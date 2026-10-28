#ifndef HFSPLUS_CATALOG_H
#define HFSPLUS_CATALOG_H

#include "hfsplus/volume.h"
#include "hfsplus/btree.h"
#include "hfsplus/hfsplus_defs.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct hfs_dirent {
    uint32_t cnid;
    bool is_directory;
    uint64_t size;
    uint32_t create_date;
    uint32_t modify_date;
    uint32_t access_date;
    uint16_t posix_mode;
    uint32_t uid;
    uint32_t gid;
    char name[512]; /* UTF-8 */
    hfs_fork_t data_fork;
    hfs_fork_t rsrc_fork;
} hfs_dirent_t;

typedef struct hfs_catalog {
    hfs_volume_t* vol;
    hfs_btree_t* btree;
} hfs_catalog_t;

/* Callback for directory enumeration. Return 0 to continue, non-zero to stop. */
typedef int (*hfs_dir_callback_fn)(const hfs_dirent_t* entry, void* user_data);

/* Open the Catalog B-Tree from a mounted volume */
hfs_catalog_t* hfs_catalog_open(hfs_volume_t* vol);

/* Lookup a child item by parent CNID and UTF-8 name */
int hfs_catalog_lookup(hfs_catalog_t* cat, uint32_t parent_cnid, const char* name, hfs_dirent_t* out_dirent);

/* Lookup an item by full path, e.g. "/System/Library/CoreServices" or "\\Users\\dmitry" */
int hfs_catalog_lookup_path(hfs_catalog_t* cat, const char* path, hfs_dirent_t* out_dirent);

/* Iterate over all children of a folder */
int hfs_catalog_iterate_dir(hfs_catalog_t* cat, uint32_t folder_cnid, hfs_dir_callback_fn callback, void* user_data);

/* Close catalog */
void hfs_catalog_close(hfs_catalog_t* cat);

#ifdef __cplusplus
}
#endif

#endif /* HFSPLUS_CATALOG_H */
