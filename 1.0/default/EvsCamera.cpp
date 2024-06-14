/*
 * Copyright (C) 2016 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#define LOG_TAG "android.hardware.automotive.evs@1.0-service"

#include "EvsCamera.h"
#include "EvsEnumerator.h"

#include <ui/GraphicBufferAllocator.h>
#include <ui/GraphicBufferMapper.h>


namespace android {
namespace hardware {
namespace automotive {
namespace evs {
namespace V1_0 {
namespace implementation {


// Special camera names for which we'll initialize alternate test data
const char EvsCamera::kCameraName_Backup[]    = "backup";


// Arbitrary limit on number of graphics buffers allowed to be allocated
// Safeguards against unreasonable resource consumption and provides a testable limit
const unsigned MAX_BUFFERS_IN_FLIGHT = 100;


EvsCamera::EvsCamera(const char *id) :
        mFramesAllowed(0),
        mFramesInUse(0),
        mStreamState(STOPPED) {

    ALOGD("EvsCamera instantiated");

    mDescription.cameraId = id;

    
    mHeight = 1280;
    mWidth = 960;
    mFps = 30;

    mFormat = HAL_PIXEL_FORMAT_RGBA_8888;
    mUsage  = GRALLOC_USAGE_HW_TEXTURE | GRALLOC_USAGE_HW_CAMERA_WRITE |
              GRALLOC_USAGE_SW_READ_RARELY | GRALLOC_USAGE_SW_WRITE_RARELY;
}

EvsCamera::EvsCamera(const char *id, ICameraSource* source) :
        mFramesAllowed(0),
        mFramesInUse(0),
        mStreamState(STOPPED), 
        mCameraSource(source) {

    int ret;

    if (!id || !source) {
        ALOGE("Invalid parameters.");
        return;
    }

    // TODO: only support fixed avm cameras.
    mWidth = 1280;
    mHeight = 960;
    mFps = 30;

    mFormat = HAL_PIXEL_FORMAT_YCBCR_422_I; // FIXME

    mStride = mWidth * 2;

    mUsage  = GRALLOC_USAGE_HW_TEXTURE | GRALLOC_USAGE_HW_CAMERA_WRITE |
              GRALLOC_USAGE_SW_READ_RARELY | GRALLOC_USAGE_SW_WRITE_RARELY;

    mDescription.cameraId = id;

    ret = mCameraSource->open();
    if (ret) {
        ALOGE("Camera[%s] source open failed.", id);
        return;
    }

    mCameraStream = mCameraSource->acquireCameraStream();
    if (!mCameraStream) {
        ALOGE("Camera[%s] acquireCameraStream failed.", id);
        return;
    }

    ICameraStream::Capability caps;

    ret = mCameraStream->queryCapabilities(caps);
    if (ret) {
        ALOGE("Camera[%s] queryCapabilities failed.", id);
        return;
    }

    std::vector<videoin_format> formats;
    
    ret = mCameraStream->getFormatList(formats);
    if (ret) {
        ALOGE("Camera[%s] getFormatList failed.", id);
        return;
    }

    for (int i = 0; i < formats.size(); i++) {
        ALOGI("Camera[%s] format[%d]: width = %d, height = %d, pixelfmt = %x, fps = %d.", 
            id, i,
            formats[i].width, formats[i].height, formats[i].pixelfmt, formats[i].fps);
    }

    // Set format.
    videoin_format format;

    format.pixelfmt = VIDEOIN_DATA_FMT_UYVY;;
    format.width = mWidth;
    format.height = mHeight;
    format.fps = mFps;

    ret = mCameraStream->config(format, ICameraStream::ALLOCATE_BUFFER_BY_USER, mFramesAllowed);
    if (ret) {
        ALOGE("Camera[%s] config stream failed.", mDescription.cameraId.c_str());
    }

    ALOGD("EvsCamera instantiated");
}

EvsCamera::~EvsCamera() {
    int ret;

    ALOGD("EvsCamera being destroyed");
    forceShutdown();

    ret = mCameraSource->close();
    if (ret) {
        ALOGE("Camera[%s] source close failed.", mDescription.cameraId.c_str());
        return;
    }

}


//
// This gets called if another caller "steals" ownership of the camera
//
void EvsCamera::forceShutdown()
{
    int ret;

    ALOGD("EvsCamera forceShutdown");

    // Make sure our output stream is cleaned up
    // (It really should be already)
    stopVideoStream();

    // Claim the lock while we work on internal state
    std::lock_guard <std::mutex> lock(mAccessLock);

    // Drop all the graphics buffers we've been using
    if (mBuffers.size() > 0) {
        GraphicBufferAllocator& alloc(GraphicBufferAllocator::get());
        for (auto&& rec : mBuffers) {
            if (rec.inUse) {
                ALOGE("Error - releasing buffer despite remote ownership");
            }
            alloc.free(rec.handle);
            rec.handle = nullptr;
        }
        mBuffers.clear();
    }

    if (mIonBuffers.size() > 0) {
        for (auto&& rec : mIonBuffers) {
            if (rec.inUse) {
                ALOGE("Error - releasing buffer despite remote ownership");
            }

            videoin_buf_output_info outInfo;
            outInfo.va = rec.info->va;
            outInfo.fd = rec.info->fd;
            outInfo.size = rec.info->size;

            ret = videoin_buffer_free(&outInfo);
            if (ret) {
                ALOGE("videoin_buffer_free failed.");
            }
            
            free(rec.info);
            rec.info = nullptr;
            free((void*)rec.handle);
            rec.handle = nullptr;

        }
        mIonBuffers.clear();
    }
    // Put this object into an unrecoverable error state since somebody else
    // is going to own the underlying camera now
    mStreamState = DEAD;
}


// Methods from ::android::hardware::automotive::evs::V1_0::IEvsCamera follow.
Return<void> EvsCamera::getCameraInfo(getCameraInfo_cb _hidl_cb) {
    ALOGD("getCameraInfo");

    // Send back our self description
    _hidl_cb(mDescription);
    return Void();
}


Return<EvsResult> EvsCamera::setMaxFramesInFlight(uint32_t bufferCount) {
    ALOGD("setMaxFramesInFlight");
    std::lock_guard<std::mutex> lock(mAccessLock);

    // If we've been displaced by another owner of the camera, then we can't do anything else
    if (mStreamState == DEAD) {
        ALOGE("ignoring setMaxFramesInFlight call when camera has been lost.");
        return EvsResult::OWNERSHIP_LOST;
    }

    // We cannot function without at least one video buffer to send data
    if (bufferCount < 1) {
        ALOGE("Ignoring setMaxFramesInFlight with less than one buffer requested");
        return EvsResult::INVALID_ARG;
    }

    // Update our internal state
    if (setAvailableFrames_Locked(bufferCount)) {
        return EvsResult::OK;
    } else {
        return EvsResult::BUFFER_NOT_AVAILABLE;
    }
}


Return<EvsResult> EvsCamera::startVideoStream(const ::android::sp<IEvsCameraStream>& stream)  {
    int ret;

    ALOGD("startVideoStream");
    std::lock_guard<std::mutex> lock(mAccessLock);

    // If we've been displaced by another owner of the camera, then we can't do anything else
    if (mStreamState == DEAD) {
        ALOGE("ignoring startVideoStream call when camera has been lost.");
        return EvsResult::OWNERSHIP_LOST;
    }
    if (mStreamState != STOPPED) {
        ALOGE("ignoring startVideoStream call when a stream is already running.");
        return EvsResult::STREAM_ALREADY_RUNNING;
    }

    // If the client never indicated otherwise, configure ourselves for a single streaming buffer
    if (mFramesAllowed < 1) {
        if (!setAvailableFrames_Locked(1)) {
            ALOGE("Failed to start stream because we couldn't get a graphics buffer");
            return EvsResult::BUFFER_NOT_AVAILABLE;
        }
    }

    if (!mCameraStream) {
        ALOGE("Failed to start stream due to underlying service error.");
        return EvsResult::UNDERLYING_SERVICE_ERROR;
    }

    // Enqueue user pre-alloc buffer.
    for (int i = 0; i < mIonBuffers.size(); i++) {
        buffer_handle_t handle = mIonBuffers[i].handle;
        ICameraStream::bufferInfo* info = mIonBuffers[i].info;

        if (!handle || !info) continue;

        ret = mCameraStream->enqueue(info);
        if (ret) {
            ALOGE("Camera[%s] enqueue stream failed.", mDescription.cameraId.c_str());
            return EvsResult::UNDERLYING_SERVICE_ERROR;
        }

        ALOGI("Camera[%s] enqueue stream buffer info: fd = %d, size = %d, va = %p.", mDescription.cameraId.c_str(),
            info->fd, info->size, info->va);
    }

    // Stream on.
    ret = mCameraStream->start();
    if (ret) {
        ALOGE("Camera[%s] start stream failed.", mDescription.cameraId.c_str());
        return EvsResult::UNDERLYING_SERVICE_ERROR;
    }

    // Record the user's callback for use when we have a frame ready
    mStream = stream;

    // Start the frame generation thread
    mStreamState = RUNNING;
    mCaptureThread = std::thread([this](){ generateFrames(); });

    return EvsResult::OK;
}


Return<void> EvsCamera::doneWithFrame(const BufferDesc& buffer)  {
    ALOGD("doneWithFrame");
    {  // lock context
        std::lock_guard <std::mutex> lock(mAccessLock);

        if (buffer.memHandle == nullptr) {
            ALOGE("ignoring doneWithFrame called with null handle");
        } else if (buffer.bufferId >= mIonBuffers.size()) {
            ALOGE("ignoring doneWithFrame called with invalid bufferId %d (max is %zu)",
                  buffer.bufferId, mIonBuffers.size() - 1);
        } else if (!mIonBuffers[buffer.bufferId].inUse) {
            ALOGE("ignoring doneWithFrame called on frame %d which is already free",
                  buffer.bufferId);
        } else {
            // Mark the frame as available
            mIonBuffers[buffer.bufferId].inUse = false;
            mFramesInUse--;

            // Enqueue buffer to camera stream.
            ICameraStream::bufferInfo* info;
            info = mIonBuffers[buffer.bufferId].info;
            int ret = mCameraStream->enqueue(info);
            if (ret) {
                ALOGE("Camera[%s] enqueue buffer id = %d failed.", mDescription.cameraId.c_str(), buffer.bufferId);
            } else {
                ALOGD("Camera[%s] enqueue buffer id = %d success.", mDescription.cameraId.c_str(), buffer.bufferId);
            }
        }

    }

    return Void();
}


Return<void> EvsCamera::stopVideoStream()  {
    int ret;
    ALOGD("stopVideoStream");
    std::unique_lock <std::mutex> lock(mAccessLock);

    if (mStreamState == RUNNING) {
        // Tell the GenerateFrames loop we want it to stop
        mStreamState = STOPPING;

        // Block outside the mutex until the "stop" flag has been acknowledged
        // We won't send any more frames, but the client might still get some already in flight
        ALOGD("Waiting for stream thread to end...");
        lock.unlock();
        mCaptureThread.join();
        lock.lock();

        ret = mCameraStream->stop();
        if (ret) {
            ALOGE("Camera[%s] stop stream failed.", mDescription.cameraId.c_str());
        }

        mStreamState = STOPPED;
        mStream = nullptr;

        ALOGD("Stream marked STOPPED.");
    }

    return Void();
}


Return<int32_t> EvsCamera::getExtendedInfo(uint32_t opaqueIdentifier)  {
    ALOGD("getExtendedInfo");
    std::lock_guard<std::mutex> lock(mAccessLock);

    // For any single digit value, return the index itself as a test value
    if (opaqueIdentifier <= 9) {
        return opaqueIdentifier;
    }

    // Return zero by default as required by the spec
    return 0;
}


Return<EvsResult> EvsCamera::setExtendedInfo(uint32_t /*opaqueIdentifier*/, int32_t /*opaqueValue*/)  {
    ALOGD("setExtendedInfo");
    std::lock_guard<std::mutex> lock(mAccessLock);

    // If we've been displaced by another owner of the camera, then we can't do anything else
    if (mStreamState == DEAD) {
        ALOGE("ignoring setExtendedInfo call when camera has been lost.");
        return EvsResult::OWNERSHIP_LOST;
    }

    // We don't store any device specific information in this implementation
    return EvsResult::INVALID_ARG;
}


bool EvsCamera::setAvailableFrames_Locked(unsigned bufferCount) {
    if (bufferCount < 1) {
        ALOGE("Ignoring request to set buffer count to zero");
        return false;
    }
    if (bufferCount > MAX_BUFFERS_IN_FLIGHT) {
        ALOGE("Rejecting buffer request in excess of internal limit");
        return false;
    }

    // Is an increase required?
    if (mFramesAllowed < bufferCount) {
        // An increase is required
        unsigned needed = bufferCount - mFramesAllowed;
        ALOGI("Allocating %d buffers for camera frames", needed);

        unsigned added = increaseAvailableFrames_Locked(needed);
        if (added != needed) {
            // If we didn't add all the frames we needed, then roll back to the previous state
            ALOGE("Rolling back to previous frame queue size");
            decreaseAvailableFrames_Locked(added);
            return false;
        }
    } else if (mFramesAllowed > bufferCount) {
        // A decrease is required
        unsigned framesToRelease = mFramesAllowed - bufferCount;
        ALOGI("Returning %d camera frame buffers", framesToRelease);

        unsigned released = decreaseAvailableFrames_Locked(framesToRelease);
        if (released != framesToRelease) {
            // This shouldn't happen with a properly behaving client because the client
            // should only make this call after returning sufficient outstanding buffers
            // to allow a clean resize.
            ALOGE("Buffer queue shrink failed -- too many buffers currently in use?");
        }
    }

    return true;
}


unsigned EvsCamera::increaseAvailableFrames_Locked(unsigned numToAdd) {
    // Acquire the graphics buffer allocator
    // GraphicBufferAllocator &alloc(GraphicBufferAllocator::get());

    unsigned added = 0;

    while (added < numToAdd) {
        #if 0
        buffer_handle_t memHandle = nullptr;
        status_t result = alloc.allocate(mWidth, mHeight, mFormat, 1, mUsage,
                                         &memHandle, &mStride, 0, "EvsCamera");
        if (result != NO_ERROR) {
            ALOGE("Error %d allocating %d x %d graphics buffer", result, mWidth, mHeight);
            break;
        }
        if (!memHandle) {
            ALOGE("We didn't get a buffer handle back from the allocator");
            break;
        }

        // Find a place to store the new buffer
        bool stored = false;
        for (auto&& rec : mBuffers) {
            if (rec.handle == nullptr) {
                // Use this existing entry
                rec.handle = memHandle;
                rec.inUse = false;
                stored = true;
                break;
            }
        }
        if (!stored) {
            // Add a BufferRecord wrapping this handle to our set of available buffers
            mBuffers.emplace_back(memHandle);
        }
        #endif

        videoin_buf_output_info outInfo;
        videoin_buf_intput_info inParam;

        inParam.fmt = VIDEOIN_BUF_FMT_UYVY;
        inParam.width = mWidth;
        inParam.height = mHeight;
        inParam.mem_type = VIDEOIN_BUF_TYPE_INTERNAL;

        int ret = videoin_buffer_alloc(&inParam, &outInfo);
        if (ret) {
            ALOGE("videoin_buffer_alloc failed.");
            break;
        }

        if (outInfo.fd <= 0 || outInfo.size <= 0) {
            ALOGE("Invalid output info.");
            break;
        }

        BufferNode bufferNode;
        bufferNode.inUse = false;
        bufferNode.info = (ICameraStream::bufferInfo*)malloc(sizeof(ICameraStream::bufferInfo));
        bufferNode.info->va = outInfo.va;
        bufferNode.info->fd = outInfo.fd;
        bufferNode.info->size = outInfo.size;
        bufferNode.info->yStride = outInfo.stride[0];
        bufferNode.info->cStride = outInfo.stride[1];
        bufferNode.handle = (native_handle_t*)malloc(sizeof(native_handle_t) + 4);

        // Find a place to store the new buffer
        bool stored = false;
        for (int i = 0; i < mIonBuffers.size(); i++) {
            if (mIonBuffers[i].info == nullptr) {
                // Use this existing entry
                bufferNode.info->bufIdx = i;
                mIonBuffers[i].info = bufferNode.info;
                mIonBuffers[i].handle = bufferNode.handle;
                mIonBuffers[i].inUse = false;
                stored = true;
                break;
            }
        }

        if (!stored) {
            bufferNode.info->bufIdx = mIonBuffers.size();
            mIonBuffers.push_back(bufferNode);
        }
        
        ALOGI("Buffer[%d] info: va = %p, fd = %d, size = %d.", bufferNode.info->bufIdx, bufferNode.info->va, bufferNode.info->fd, bufferNode.info->size);

        mFramesAllowed++;
        added++;
    }

    return added;
}


unsigned EvsCamera::decreaseAvailableFrames_Locked(unsigned numToRemove) {
    // Acquire the graphics buffer allocator
    // GraphicBufferAllocator &alloc(GraphicBufferAllocator::get());

    unsigned removed = 0;

    #if 0
    for (auto&& rec : mBuffers) {
        // Is this record not in use, but holding a buffer that we can free?
        if ((rec.inUse == false) && (rec.handle != nullptr)) {
            // Release buffer and update the record so we can recognize it as "empty"
            alloc.free(rec.handle);
            rec.handle = nullptr;

            mFramesAllowed--;
            removed++;

            if (removed == numToRemove) {
                break;
            }
        }
    }
    #endif
    for (auto&& rec : mIonBuffers) {
        // Is this record not in use, but holding a buffer that we can free?
        if ((rec.inUse == false) && (rec.handle != nullptr) && (rec.info != nullptr)) {
            // Release buffer and update the record so we can recognize it as "empty"
            videoin_buf_output_info outInfo;
            outInfo.va = rec.info->va;
            outInfo.fd = rec.info->fd;
            outInfo.size = rec.info->size;

            int ret = videoin_buffer_free(&outInfo);
            if (ret) {
                ALOGE("videoin_buffer_free failed.");
            }

            free((void*)rec.handle);
            free(rec.info);

            rec.info = nullptr;
            rec.handle = nullptr;

            mFramesAllowed--;
            removed++;

            if (removed == numToRemove) {
                break;
            }
        }
    }


    return removed;
}


// This is the asynchronous frame generation thread that runs in parallel with the
// main serving thread.  There is one for each active camera instance.
void EvsCamera::generateFrames() {
    ALOGD("Frame generation loop started");
    int ret;
    unsigned idx;

    while (true) {
        bool timeForFrame = false;
        nsecs_t startTime = systemTime(SYSTEM_TIME_MONOTONIC);

        // Lock scope for updating shared state
        {
            std::lock_guard<std::mutex> lock(mAccessLock);

            if (mStreamState != RUNNING) {
                // Break out of our main thread loop
                break;
            }

            // Are we allowed to issue another buffer?
            if (mFramesInUse >= mFramesAllowed) {
                // Can't do anything right now -- skip this frame
                ALOGW("Skipped a frame because too many are in flight\n");
            } else {
                // Identify an available buffer to fill
                timeForFrame = true;
            }
        }

        if (timeForFrame) {
            // Assemble the buffer description we'll transmit below
            BufferDesc buff = {};
            buff.width      = mWidth;
            buff.height     = mHeight;
            buff.stride     = mStride;
            buff.format     = mFormat;
            buff.usage      = mUsage;
            buff.memHandle = nullptr;
            
            ICameraStream::bufferInfo bufferInfo;
            ret = mCameraStream->dequeue(&bufferInfo);
            if (ret) {
                ALOGE("Camera[%s] stream dequeue failed.", mDescription.cameraId.c_str());
                usleep(10 * 1000); // 10ms
                continue;
            }

            ALOGD("Camera[%s] dequeue buffer info: index = %d, fd = %d, size = %d, va = %p, pa = %p.", 
                mDescription.cameraId.c_str(),
                bufferInfo.bufIdx, bufferInfo.fd, bufferInfo.size, bufferInfo.va, bufferInfo.pa);

            // NOTE: we want change iterator, so use reference!!!
            for(auto &rec: mIonBuffers) {
                if (rec.info->bufIdx == bufferInfo.bufIdx) {
                    buff.memHandle = rec.handle;
                    rec.inUse = true;
                    mFramesInUse++;
                    ALOGD("Found buffer index = %d fd = %d, set it in use.", bufferInfo.bufIdx, bufferInfo.fd);
                }
            }

            if (!buff.memHandle) {
                ALOGE("Invalid buffer handle at index = %d.", bufferInfo.bufIdx);
                continue;
            }

            idx = bufferInfo.bufIdx;
            buff.bufferId = idx;

            #ifdef VIDEO_DUMP
            FILE* fp = fopen("./evs_stream.uyvy", "a+");
            if (fp) {
                fwrite(bufferInfo.va, 1, bufferInfo.size, fp);
                fclose(fp);
            }
            #endif

            native_handle_t* t = const_cast<native_handle_t*>(buff.memHandle.getNativeHandle());
            t->version = sizeof(native_handle_t);
            t->numFds = 1;
            t->numInts = 0;
            t->data[0] = bufferInfo.fd; // Set fd to native handle

            // Issue the (asynchronous) callback to the client -- can't be holding the lock
            auto result = mStream->deliverFrame(buff);
            if (result.isOk()) {
                ALOGD("Delivered %p as id %d", buff.memHandle.getNativeHandle(), buff.bufferId);
            } else {
                // This can happen if the client dies and is likely unrecoverable.
                // To avoid consuming resources generating failing calls, we stop sending
                // frames.  Note, however, that the stream remains in the "STREAMING" state
                // until cleaned up on the main thread.
                ALOGE("Frame delivery call failed in the transport layer.");

                // Since we didn't actually deliver it, mark the frame as available
                std::lock_guard<std::mutex> lock(mAccessLock);
                mIonBuffers[idx].inUse = false;
                mFramesInUse--;

                break;
            }
        }

        // We arbitrarily choose to generate frames at 12 fps to ensure we pass the 10fps test requirement
        static const int kTargetFrameRate = mFps;
        static const nsecs_t kTargetFrameTimeUs = 1000*1000 / kTargetFrameRate;
        const nsecs_t now = systemTime(SYSTEM_TIME_MONOTONIC);
        const nsecs_t workTimeUs = (now - startTime) / 1000;
        const nsecs_t sleepDurationUs = kTargetFrameTimeUs - workTimeUs;
        if (sleepDurationUs > 0) {
            usleep(sleepDurationUs);
        }
    }

    // If we've been asked to stop, send one last NULL frame to signal the actual end of stream
    BufferDesc nullBuff = {};
    auto result = mStream->deliverFrame(nullBuff);
    if (!result.isOk()) {
        ALOGE("Error delivering end of stream marker");
    }

    return;
}


void EvsCamera::fillTestFrame(const BufferDesc& buff) {
    // Lock our output buffer for writing
    uint32_t *pixels = nullptr;
    GraphicBufferMapper &mapper = GraphicBufferMapper::get();
    mapper.lock(buff.memHandle,
                GRALLOC_USAGE_SW_WRITE_OFTEN | GRALLOC_USAGE_SW_READ_NEVER,
                android::Rect(buff.width, buff.height),
                (void **) &pixels);

    // If we failed to lock the pixel buffer, we're about to crash, but log it first
    if (!pixels) {
        ALOGE("Camera failed to gain access to image buffer for writing");
    }

    // Fill in the test pixels
    for (unsigned row = 0; row < buff.height; row++) {
        for (unsigned col = 0; col < buff.width; col++) {
            // Index into the row to check the pixel at this column.
            // We expect 0xFF in the LSB channel, a vertical gradient in the
            // second channel, a horitzontal gradient in the third channel, and
            // 0xFF in the MSB.
            // The exception is the very first 32 bits which is used for the
            // time varying frame signature to avoid getting fooled by a static image.
            uint32_t expectedPixel = 0xFF0000FF           | // MSB and LSB
                                     ((row & 0xFF) <<  8) | // vertical gradient
                                     ((col & 0xFF) << 16);  // horizontal gradient
            if ((row | col) == 0) {
                static uint32_t sFrameTicker = 0;
                expectedPixel = (sFrameTicker) & 0xFF;
                sFrameTicker++;
            }
            pixels[col] = expectedPixel;
        }
        // Point to the next row
        // NOTE:  stride retrieved from gralloc is in units of pixels
        pixels = pixels + buff.stride;
    }

    // Release our output buffer
    mapper.unlock(buff.memHandle);
}


} // namespace implementation
} // namespace V1_0
} // namespace evs
} // namespace automotive
} // namespace hardware
} // namespace android
