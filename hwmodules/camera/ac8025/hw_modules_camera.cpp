// System headers.
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include <log/log.h>
#include <cutils/klog.h>

// Platform provided headers.
#include "videoin_defs.h"

#include "ICameraSource.h"
#include "ICameraManager.h"

// Cross-platform headers.
#include "hwmodules.h"

#ifdef LOGTAG
#undef LOGTAG
#endif

#define LOGTAG "evs.camera"

static ICameraManager* sCameraManager = nullptr;

const static std::map<camera_fmt_t, uint32_t> kCameraFormat = {
    {camera_fmt_t::CAMERA_FMT_YUYV, VIDEOIN_DATA_FMT_YUYV},
    {camera_fmt_t::CAMERA_FMT_UYVY, VIDEOIN_DATA_FMT_UYVY},
    {camera_fmt_t::CAMERA_FMT_YV12, VIDEOIN_DATA_FMT_YVU420},
    {camera_fmt_t::CAMERA_FMT_YU12, VIDEOIN_DATA_FMT_YUV420},
    {camera_fmt_t::CAMERA_FMT_NV21, VIDEOIN_DATA_FMT_NV12},
    {camera_fmt_t::CAMERA_FMT_NV12, VIDEOIN_DATA_FMT_NV21},
};

const static std::map<int, camera_info_t> kCameraInfo = {
    { 10, { camera_type_t::CAMERA_TYPE_AVM, 0x00000002, camera_fmt_t::CAMERA_FMT_UYVY, 1280, 960, 25 }},
    { 11, { camera_type_t::CAMERA_TYPE_AVM, 0x00000002, camera_fmt_t::CAMERA_FMT_UYVY, 1280, 960, 25 }},
    { 12, { camera_type_t::CAMERA_TYPE_AVM, 0x00000002, camera_fmt_t::CAMERA_FMT_UYVY, 1280, 960, 25 }},
    { 13, { camera_type_t::CAMERA_TYPE_AVM, 0x00000002, camera_fmt_t::CAMERA_FMT_UYVY, 1280, 960, 25 }},
};

std::vector<uint32_t> getCameraList(void)
{
    std::vector<uint32_t> cameraList;

    if (!sCameraManager) {
        ALOGE("Invalid camera manager.");
        goto __error;
    }

    sCameraManager->getCameraList(cameraList);

__error:
    return cameraList;
}

camera_info_t getCameraInfo(uint32_t id)
{
    camera_info_t info;
    
    memset(&info, 0x00, sizeof(camera_info_t));

    CameraStaticInfo staticInfo;    
    sCameraManager->getCameraInfo(id, staticInfo);

    ALOGI("Camera[%d] info: width = %d, height = %d,  fps = %d.", id, staticInfo.width, staticInfo.height, staticInfo.fps);

    info.width = staticInfo.width;
    info.height = staticInfo.height;
    info.fps = staticInfo.fps;
    // TODO: add other fields.

    return info;
}

static int startVideo(camera_stream_t* stream)
{
    int ret = 0;
    ICameraStream* camera_stream = nullptr;

    if (!stream) {
        ALOGE("Invalid stream.");
        goto __error;
    }

    camera_stream = (ICameraStream*)stream->stream_handle;

    ret = camera_stream->start();
    if (ret) {
        ALOGE("Failed to start video.");
        goto __error;
    }

    return 0;
__error:
    return -1;
}

static int queueBuffer(camera_stream_t* stream, camera_buffer_t* buffer)
{
    int ret = 0;
    ICameraStream* camera_stream = nullptr;
    ICameraStream::bufferInfo buffer_info;

    if (!stream || !buffer) {
        ALOGE("Invalid argument(s).");
        goto __error;
    }

    camera_stream = (ICameraStream*)stream->stream_handle;

    buffer_info.bufIdx = buffer->buf_idx;
    buffer_info.fd = buffer->fd;
    buffer_info.size = buffer->size;
    buffer_info.yStride = buffer->y_stride;
    buffer_info.cStride = buffer->u_stride;
    buffer_info.va = buffer->va;
    buffer_info.pa = buffer->pa;

    ret = camera_stream->enqueue(&buffer_info);
    if (ret) {
        ALOGE("Failed to queue buffer.");
        goto __error;
    }

    return 0;
__error:
    return -1;
}

static int dequeueBuffer(camera_stream_t* stream, camera_buffer_t* buffer)
{
    int ret = 0;
    ICameraStream* camera_stream = nullptr;
    ICameraStream::bufferInfo buffer_info;

    if (!stream || !buffer) {
        ALOGE("Invalid argument(s).");
        goto __error;
    }

    camera_stream = (ICameraStream*)stream->stream_handle;

    ret = camera_stream->dequeue(&buffer_info);
    if (ret) {
        ALOGE("Failed to dequeue buffer.");
        goto __error;
    }

    buffer->buf_idx = buffer_info.bufIdx;
    buffer->fd = buffer_info.fd;
    buffer->size = buffer_info.size;
    buffer->y_stride = buffer_info.yStride;
    buffer->u_stride = buffer_info.cStride;
    buffer->va = buffer_info.va;
    buffer->pa = buffer_info.pa;
    buffer->timestamp = buffer_info.timestamp;

    return 0;
__error:
    return -1;
}

static int stopVideo(camera_stream_t* stream)
{
    int ret = 0;
    ICameraStream* camera_stream = nullptr;

    if (!stream) {
        ALOGE("Invalid stream.");
        goto __error;
    }

    camera_stream = (ICameraStream*)stream->stream_handle;

    ret = camera_stream->stop();
    if (ret) {
        ALOGE("Failed to stop video.");
        goto __error;
    }

    return 0;
__error:
    return -1;
}

camera_stream_t* open(uint32_t id)
{
    int ret = 0;
    camera_info_t info;
    ICameraSource* camera_source = nullptr;
    ICameraStream* camera_stream = nullptr;
    videoin_format format;

    camera_stream_t* stream = (camera_stream_t*)calloc(1, sizeof(camera_stream_t));
    if (!stream) {
        ALOGE("Failed to allocate memory for camera stream.");
        goto __error;
    }

    stream->start_video = startVideo;
    stream->queue_buffer = queueBuffer;
    stream->dequeue_buffer = dequeueBuffer;
    stream->stop_video = stopVideo;

    camera_source = sCameraManager->getCameraInstance(id);
    if (!camera_source) {
        ALOGE("Failed to get camera[%d] instance.", id);
        goto __error;
    }

    ret = camera_source->open();
    if (ret) {
        ALOGE("Failed to open camera[%d].", id);
        goto __error;
    }

    camera_stream = camera_source->acquireCameraStream();
    if (!camera_stream) {
        ALOGE("Failed to acquire camera[%d] stream.", id);
        goto __error;
    }

    stream->source_handle = (void*)camera_source;
    stream->stream_handle = (void*)camera_stream;

    if (kCameraInfo.find(id) == kCameraInfo.end()) {
        ALOGE("Invalid camera[%d] info.", id);
        goto __error;
    }

    info = kCameraInfo.at(id);

    if (kCameraFormat.find(info.format) == kCameraFormat.end()) {
        ALOGE("Invalid camera format[%d].", info.format);
        goto __error;
    }
    
    format.pixelfmt = kCameraFormat.at(info.format);

    format.width = info.width;
    format.height = info.height;
    format.fps = info.fps;

    ALOGD("Config camera[%d] stream: %dx%d, fps:%d.", id, info.width, info.height, info.fps);

    ret = camera_stream->config(format);
    if (ret) {
        ALOGE("Failed to config camera[%d].", id);
        goto __error;
    }

    return stream;
__error:
    if (stream->stream_handle) {
        camera_source->releaseCameraStream((ICameraStream*)stream->stream_handle);
    }

    if (camera_source) {
        camera_source->close();
    }

    if (sCameraManager) {
        if (camera_source) {
            sCameraManager->freeCameraInstance(camera_source);
        }
    }
    
    if (stream) free(stream);

    return nullptr;
}

int close(camera_stream_t* stream)
{
    ICameraSource* camera_source;
    ICameraStream* camera_stream;

    if (!stream) {
        ALOGE("Invalid stream.");
        goto __error;
    }

    if (!stream->source_handle) {
        ALOGE("Invalid camera source.");
        goto __error;
    }

    if (!stream->stream_handle) {
        ALOGE("Invalid camera stream.");
        goto __error;
    }

    camera_source = (ICameraSource*)stream->source_handle;
    camera_stream = (ICameraStream*)stream->stream_handle;

    camera_source->releaseCameraStream(camera_stream);
    camera_source->close();

    sCameraManager->freeCameraInstance(camera_source);

    free(stream);

    return 0;
__error:
    return -1;
}

static const camera_interface_t sCameraInterface = {
        sizeof(camera_interface_t),
        getCameraList,
        getCameraInfo,
        open,
        close
};

static const camera_interface_t *camera__get_camera_interface() {
    return &sCameraInterface;
}

static int open_camera(const struct hw_module_t *module, char const *name, struct hw_device_t **device)
{
    struct camera_device_t *dev = (struct camera_device_t *)malloc(sizeof(struct camera_device_t));
    memset(dev, 0x00, sizeof(camera_device_t));

    if (!module || !name || !device) {
        ALOGE("Invalid arguments.");
        return -1;
    }

    dev->common.tag = HARDWARE_DEVICE_TAG;
    dev->common.version = 0;
    dev->common.module = (struct hw_module_t *)module;
    dev->get_camera_interface = camera__get_camera_interface;

    *device = (struct hw_device_t *) dev;

    if (!sCameraManager) {
        sCameraManager = ICameraManager::getCameraManager();
        if (!sCameraManager) {
            ALOGE("Failed to get camera manager");
            return -1;
        }
    }
    
    return 0;
}

static struct hw_module_methods_t camera_module_methods = {
        .open = open_camera
};

struct hw_module_t HAL_MODULE_INFO_SYM = {
        .tag = HARDWARE_MODULE_TAG,
        .version_major = 1,
        .version_minor = 0,
        .id = CAMERA_HARDWARE_MODULE_ID,
        .name = "Evs Camera Module",
        .author = "ragehao@github",
        .methods = &camera_module_methods,
};




