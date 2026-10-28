#ifndef HFSPLUS_WINFSP_H
#define HFSPLUS_WINFSP_H

#include "hfsplus/volume.h"
#include "hfsplus/catalog.h"

#ifdef _WIN32
#include <winfsp/winfsp.h>

typedef struct hfs_winfsp_context {
    hfs_block_device_t* dev;
    hfs_volume_t* vol;
    hfs_catalog_t* cat;
    FSP_FILE_SYSTEM* fsp;
} hfs_winfsp_context_t;

typedef struct hfs_winfsp_file_context {
    hfs_dirent_t dirent;
} hfs_winfsp_file_context_t;

/* Initialize WinFsp interface and mount HFS+ filesystem */
int hfs_winfsp_mount(const wchar_t* device_or_image_path, const wchar_t* mount_point, bool debug_mode);

#endif /* _WIN32 */

#endif /* HFSPLUS_WINFSP_H */
