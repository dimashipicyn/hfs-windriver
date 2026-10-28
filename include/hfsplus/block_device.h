#ifndef HFSPLUS_BLOCK_DEVICE_H
#define HFSPLUS_BLOCK_DEVICE_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct hfs_block_device hfs_block_device_t;

typedef struct hfs_block_device_ops {
    /* Read 'count' bytes starting at 'offset' from device into 'buffer'. Returns 0 on success, negative error code on failure. */
    int (*read)(hfs_block_device_t* dev, uint64_t offset, void* buffer, size_t count);
    /* Get total capacity in bytes */
    uint64_t (*get_size)(hfs_block_device_t* dev);
    /* Flush any pending caches (if applicable) */
    int (*flush)(hfs_block_device_t* dev);
    /* Close and release backend resources */
    void (*close)(hfs_block_device_t* dev);
} hfs_block_device_ops_t;

struct hfs_block_device {
    const hfs_block_device_ops_t* ops;
    void* backend_data;
    uint32_t sector_size;
    uint64_t total_size;
};

/* Open a file / raw image on POSIX (macOS, Linux) */
hfs_block_device_t* hfs_block_device_open_file(const char* path);

#if defined(_WIN32)
/* Open physical disk or file image on Windows User-mode */
hfs_block_device_t* hfs_block_device_open_win32(const wchar_t* path);
#endif

/* Read bytes safely from block device */
static inline int hfs_block_device_read(hfs_block_device_t* dev, uint64_t offset, void* buffer, size_t count) {
    if (!dev || !dev->ops || !dev->ops->read) return -1;
    return dev->ops->read(dev, offset, buffer, count);
}

/* Close block device and free wrapper */
void hfs_block_device_close(hfs_block_device_t* dev);

#ifdef __cplusplus
}
#endif

#endif /* HFSPLUS_BLOCK_DEVICE_H */
