LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)
LOCAL_MODULE_RELATIVE_PATH := hw
LOCAL_VENDOR_MODULE := true
LOCAL_SHARED_LIBRARIES := \
    liblog \
    libcutils \
    libhardware \
    libcamera.source

LOCAL_C_INCLUDES := \
        $(LOCAL_PATH)/../../include

LOCAL_SRC_FILES := hw_modules_camera.cpp

LOCAL_MODULE := evs.camera.$(TARGET_BOARD_PLATFORM)
LOCAL_MODULE_TAGS := optional

LOCAL_CFLAGS += -Werror -Wno-error=unused-parameter -Wno-error=unused-variable -Wno-error=deprecated-declarations -Wall

include $(BUILD_SHARED_LIBRARY)