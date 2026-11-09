#ifndef VDEV_IOCTL_COMMANDS_H
#define VDEV_IOCTL_COMMANDS_H

#ifdef __KERNEL__
#include <linux/ioctl.h>
#include <linux/types.h>
#else
#include <linux/types.h>
#include <sys/ioctl.h>
#endif

#define VDEV_MAGIC 'V'
#define VDEV_BUFFER_SIZE 4096
#define VDEV_MIN_INTERVAL_MS 10
#define VDEV_MAX_INTERVAL_MS 60000
#define VDEV_VERSION 1

struct vdev_info {
    char name[32];
    __u32 version;
    __u32 buffer_size;
};

struct vdev_status {
    __u32 buffered_bytes;
    __u32 sample_interval_ms;
    __u32 sequence;
    __u32 generating;
};

struct vdev_stats {
    __u64 bytes_read;
    __u64 bytes_written;
    __u64 samples_generated;
    __u64 samples_dropped;
    __u64 opens;
};

#define VDEV_GET_DEVICE_INFO _IOR(VDEV_MAGIC, 1, struct vdev_info)
#define VDEV_GET_STATUS _IOR(VDEV_MAGIC, 2, struct vdev_status)
#define VDEV_SET_SAMPLE_INTERVAL _IOW(VDEV_MAGIC, 3, __u32)
#define VDEV_RESET_DEVICE _IO(VDEV_MAGIC, 4)
#define VDEV_CLEAR_BUFFER _IO(VDEV_MAGIC, 5)
#define VDEV_GET_STATS _IOR(VDEV_MAGIC, 6, struct vdev_stats)

#endif
