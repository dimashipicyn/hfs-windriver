#include "hfsplus/block_device.h"
#include "hfsplus/mem.h"

#include <stdio.h>
#include <errno.h>

#if defined(_WIN32)
#include <windows.h>
#include <winioctl.h>

typedef struct {
    HANDLE handle;
    bool is_device;
} win32_block_dev_t;

static int win32_read(hfs_block_device_t* dev, uint64_t offset, void* buffer, size_t count) {
    win32_block_dev_t* wdev = (win32_block_dev_t*)dev->backend_data;
    if (!wdev || wdev->handle == INVALID_HANDLE_VALUE) return -1;
    if (count == 0) return 0;

    uint32_t sector_size = dev->sector_size ? dev->sector_size : 512;

    /* Align offset and count to sector size boundaries for raw physical disk handles */
    uint64_t start_aligned = (offset / sector_size) * sector_size;
    uint64_t end_aligned = ((offset + count + sector_size - 1) / sector_size) * sector_size;
    size_t total_aligned = (size_t)(end_aligned - start_aligned);

    uint8_t* bounce = (uint8_t*)hfs_alloc(total_aligned);
    if (!bounce) return -1;

    LARGE_INTEGER li;
    li.QuadPart = (LONGLONG)start_aligned;
    if (!SetFilePointerEx(wdev->handle, li, NULL, FILE_BEGIN)) {
        hfs_free(bounce);
        return -1;
    }

    DWORD bytes_read = 0;
    if (!ReadFile(wdev->handle, bounce, (DWORD)total_aligned, &bytes_read, NULL) || bytes_read != total_aligned) {
        hfs_free(bounce);
        return -1;
    }

    size_t lead_in = (size_t)(offset - start_aligned);
    memcpy(buffer, bounce + lead_in, count);
    hfs_free(bounce);

    return 0;
}

static uint64_t win32_get_size(hfs_block_device_t* dev) {
    return dev->total_size;
}

static void win32_close(hfs_block_device_t* dev) {
    win32_block_dev_t* wdev = (win32_block_dev_t*)dev->backend_data;
    if (wdev) {
        if (wdev->handle != INVALID_HANDLE_VALUE) {
            CloseHandle(wdev->handle);
        }
        hfs_free(wdev);
    }
}

static const hfs_block_device_ops_t g_win32_ops = {
    .read = win32_read,
    .get_size = win32_get_size,
    .flush = NULL,
    .close = win32_close,
};

hfs_block_device_t* hfs_block_device_open_win32(const wchar_t* path) {
    HANDLE h = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
                           OPEN_EXISTING, 0, NULL);
    if (h == INVALID_HANDLE_VALUE) {
        return NULL;
    }

    /* Allow raw DASD disk access */
    DWORD dummy = 0;
    DeviceIoControl(h, FSCTL_ALLOW_EXTENDED_DASD_IO, NULL, 0, NULL, 0, &dummy, NULL);

    uint32_t sector_size = 512;
    uint64_t total_size = 0;

    /* Query disk geometry for sector size */
    DISK_GEOMETRY_EX geom;
    if (DeviceIoControl(h, IOCTL_DISK_GET_DRIVE_GEOMETRY_EX, NULL, 0,
                        &geom, sizeof(geom), &dummy, NULL)) {
        if (geom.Geometry.BytesPerSector > 0) {
            sector_size = geom.Geometry.BytesPerSector;
        }
        total_size = (uint64_t)geom.DiskSize.QuadPart;
    }

    /* Query precise length if available */
    GET_LENGTH_INFORMATION length_info;
    if (DeviceIoControl(h, IOCTL_DISK_GET_LENGTH_INFO, NULL, 0,
                        &length_info, sizeof(length_info), &dummy, NULL)) {
        total_size = (uint64_t)length_info.Length.QuadPart;
    }

    /* If IOCTLs failed (e.g. regular file / .dmg), fallback to GetFileSizeEx */
    if (total_size == 0) {
        LARGE_INTEGER size;
        if (GetFileSizeEx(h, &size)) {
            total_size = (uint64_t)size.QuadPart;
        } else {
            CloseHandle(h);
            return NULL;
        }
    }

    hfs_block_device_t* dev = (hfs_block_device_t*)hfs_zalloc(sizeof(hfs_block_device_t));
    win32_block_dev_t* wdev = (win32_block_dev_t*)hfs_zalloc(sizeof(win32_block_dev_t));
    if (!dev || !wdev) {
        CloseHandle(h);
        hfs_free(dev);
        hfs_free(wdev);
        return NULL;
    }

    wdev->handle = h;
    wdev->is_device = true;

    dev->ops = &g_win32_ops;
    dev->backend_data = wdev;
    dev->sector_size = sector_size;
    dev->total_size = total_size;

    return dev;
}

hfs_block_device_t* hfs_block_device_open_file(const char* path) {
    if (!path) return NULL;
    wchar_t wpath[MAX_PATH];
    if (MultiByteToWideChar(CP_UTF8, 0, path, -1, wpath, MAX_PATH) == 0) {
        return NULL;
    }
    return hfs_block_device_open_win32(wpath);
}

#elif !defined(_KERNEL_MODE)
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>

typedef struct {
    int fd;
} posix_block_dev_t;

static int posix_read(hfs_block_device_t* dev, uint64_t offset, void* buffer, size_t count) {
    posix_block_dev_t* pdev = (posix_block_dev_t*)dev->backend_data;
    if (!pdev || pdev->fd < 0) return -1;

    uint8_t* dst = (uint8_t*)buffer;
    size_t total_read = 0;

    while (total_read < count) {
        ssize_t res = pread(pdev->fd, dst + total_read, count - total_read, (off_t)(offset + total_read));
        if (res < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        if (res == 0) {
            return -1;
        }
        total_read += (size_t)res;
    }

    return 0;
}

static uint64_t posix_get_size(hfs_block_device_t* dev) {
    return dev->total_size;
}

static void posix_close(hfs_block_device_t* dev) {
    posix_block_dev_t* pdev = (posix_block_dev_t*)dev->backend_data;
    if (pdev) {
        if (pdev->fd >= 0) {
            close(pdev->fd);
        }
        hfs_free(pdev);
    }
}

static const hfs_block_device_ops_t g_posix_ops = {
    .read = posix_read,
    .get_size = posix_get_size,
    .flush = NULL,
    .close = posix_close,
};

hfs_block_device_t* hfs_block_device_open_file(const char* path) {
    int fd = open(path, O_RDONLY);
    if (fd < 0) {
        return NULL;
    }

    struct stat st;
    if (fstat(fd, &st) < 0) {
        close(fd);
        return NULL;
    }

    hfs_block_device_t* dev = (hfs_block_device_t*)hfs_zalloc(sizeof(hfs_block_device_t));
    posix_block_dev_t* pdev = (posix_block_dev_t*)hfs_zalloc(sizeof(posix_block_dev_t));
    if (!dev || !pdev) {
        close(fd);
        hfs_free(dev);
        hfs_free(pdev);
        return NULL;
    }

    pdev->fd = fd;
    dev->ops = &g_posix_ops;
    dev->backend_data = pdev;
    dev->sector_size = 512;
    dev->total_size = (uint64_t)st.st_size;

    return dev;
}

#endif /* !_WIN32 && !_KERNEL_MODE */

void hfs_block_device_close(hfs_block_device_t* dev) {
    if (!dev) return;
    if (dev->ops && dev->ops->close) {
        dev->ops->close(dev);
    }
    hfs_free(dev);
}
