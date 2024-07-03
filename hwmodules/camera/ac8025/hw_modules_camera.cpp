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

// static ICameraSource* sCamSource = nullptr;
// static ICameraStream* sCamStream = nullptr;

const static std::map<camera_fmt_t, uint32_t> kCameraFormat = {
    {camera_fmt_t::CAMERA_FMT_YUYV, VIDEOIN_DATA_FMT_YUYV},
    {camera_fmt_t::CAMERA_FMT_UYVY, VIDEOIN_DATA_FMT_UYVY},
    {camera_fmt_t::CAMERA_FMT_YV12, VIDEOIN_DATA_FMT_YVU420},
    {camera_fmt_t::CAMERA_FMT_YU12, VIDEOIN_DATA_FMT_YUV420},
    {camera_fmt_t::CAMERA_FMT_NV21, VIDEOIN_DATA_FMT_NV12},
    {camera_fmt_t::CAMERA_FMT_NV12, VIDEOIN_DATA_FMT_NV21},
};



std::vector<int> getCameraList(void)
{
    std::vector<int> cameraList;



    return cameraList;
}

camera_info_t getCameraInfo(int id)
{
    camera_info_t info;
    memset(&info, 0x00, sizeof(camera_info_t));




    return info;
}

camera_instance_t* open(int id)
{
    return nullptr;
}

int close(int id)
{
    return 0;
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

    dev->common.tag = HARDWARE_DEVICE_TAG;
    dev->common.version = 0;
    dev->common.module = (struct hw_module_t *)module;
    dev->get_camera_interface = camera__get_camera_interface;

    *device = (struct hw_device_t *) dev;

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




