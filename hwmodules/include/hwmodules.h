#ifndef __HW_MODULES_H
#define __HW_MODULES_H

#include <hardware/hardware.h>


/**
 * The id of camera module
 */
#define CAMERA_HARDWARE_MODULE_ID "evs.camera"

enum camera_fmt_t : int32_t {
    CAMERA_FMT_YUYV = 0,
    CAMERA_FMT_UYVY = 1,
    CAMERA_FMT_YV12 = 2,
    CAMERA_FMT_YU12 = 3,
    CAMERA_FMT_NV21 = 4,
    CAMERA_FMT_NV12 = 5,
    CAMERA_FMT_MAX  = 6,
};

enum camera_type_t : int32_t {
    CAMERA_TYPE_RVC = 0,
    CAMERA_TYPE_AVM_FRONT = 1,
    CAMERA_TYPE_AVM_REAR = 2,
    CAMERA_TYPE_AVM_LEFT = 3,
    CAMERA_TYPE_AVM_RIGHT = 4,
    CAMERA_TYPE_MAX,
};

struct camera_buffer_t {
    unsigned int buf_idx;   /**<Buffer index*/
    int fd;                 /**<Buffer fd*/
    unsigned int size;      /**<Buffer size in byte*/
    unsigned int y_stride;  /**<Data y stride*/
    unsigned int c_Stride;  /**<Data c stride*/
    void* va;               /**<IO Virtual memory address*/
    void* pa;               /**<Buffer physical memory address. Reserved*/
    int64_t timestamp;      /**<Data timestamp*/
};

struct camera_info_t {
    enum camera_type_t type;
    uint32_t vendorFlags;
    enum camera_fmt_t format;
    uint32_t width;
    uint32_t height;
};

struct camera_instance_t {
    /** Starts video stream. */
    int (*start_video)(struct camera_instance_t* instance);

    /** Enqueue camera buffer. */
    int (*queue_buffer)(struct camera_instance_t* instance, camera_buffer_t *buffer);

    /** Dequeue camera buffer. */
    int (*dequeue_buffer)(struct camera_instance_t* instance, camera_buffer_t *buffer);

    /** Stops video stream. */
    int (*stop_video)(struct camera_instance_t* instance);
};

struct camera_interface_t {
    size_t size;

    /** Get the camera list. */
    std::vector<int> (*get_camera_list)(void);

    /** Get the given camera information. */
    camera_info_t (*get_camera_info)(int id);

    /** Opens the camera device. */
    camera_instance_t* (*open)(int id);

    /** Closes the camera device. */
    int (*close)(int id);
};

struct camera_device_t {
    struct hw_device_t common;
    const struct camera_interface_t* (*get_camera_interface)();
};

#endif
