#ifndef __HW_MODULES_H__
#define __HW_MODULES_H__

#include <hardware/hardware.h>


/**
 * The id of camera module
 */
#define CAMERA_HARDWARE_MODULE_ID "evs.camera"

typedef enum camera_fmt_t : int32_t {
    CAMERA_FMT_YUYV = 0,
    CAMERA_FMT_UYVY = 1,
    CAMERA_FMT_YV12 = 2,
    CAMERA_FMT_YU12 = 3,
    CAMERA_FMT_NV21 = 4,
    CAMERA_FMT_NV12 = 5,
    CAMERA_FMT_MAX  = 6,
} CameraFmt;

typedef enum camera_type_t : int32_t {
    CAMERA_TYPE_RVC = 0,
    CAMERA_TYPE_AVM = 1,
    CAMERA_TYPE_MAX,
} CameraType;

typedef struct camera_buffer_t {
    unsigned int buf_idx;   /**<Buffer index*/
    int fd;                 /**<Buffer fd*/
    unsigned int size;      /**<Buffer size in byte*/
    unsigned int y_stride;  /**<Data y stride*/
    unsigned int u_stride;  /**<Data u stride*/
    unsigned int v_stride;  /**<Data v stride*/
    void* va;               /**<IO Virtual memory address*/
    void* pa;               /**<Buffer physical memory address. Reserved*/
    int64_t timestamp;      /**<Data timestamp*/
} CameraBuffer;

typedef struct camera_info_t {
    enum camera_type_t type;
    uint32_t vendorFlags;
    enum camera_fmt_t format;
    uint32_t width;
    uint32_t height;
    uint32_t fps;
} CameraInfo;

typedef struct camera_stream_t {
    void* source_handle;
    void* stream_handle;

    /** Starts video stream. */
    int (*start_video)(struct camera_stream_t* stream);

    /** Enqueue camera buffer. */
    int (*queue_buffer)(struct camera_stream_t* stream, camera_buffer_t *buffer);

    /** Dequeue camera buffer. */
    int (*dequeue_buffer)(struct camera_stream_t* stream, camera_buffer_t *buffer);

    /** Stops video stream. */
    int (*stop_video)(struct camera_stream_t* stream);
} CameraStream;

typedef struct camera_interface_t {
    size_t size;

    /** Get the camera list. */
    std::vector<uint32_t> (*get_camera_list)(void);

    /** Get the given camera information. */
    camera_info_t (*get_camera_info)(uint32_t id);

    /** Opens the camera device. */
    camera_stream_t* (*open)(uint32_t id);

    /** Closes the camera device. */
    int (*close)(camera_stream_t* stream);
} CameraInterface;

typedef struct camera_device_t {
    struct hw_device_t common;
    const struct camera_interface_t* (*get_camera_interface)();
} CameraDevice;

#endif
