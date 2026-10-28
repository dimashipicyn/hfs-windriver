#ifndef HFSPLUS_EXTENTS_H
#define HFSPLUS_EXTENTS_H

#include "hfsplus/volume.h"
#include "hfsplus/btree.h"
#include "hfsplus/hfsplus_defs.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct hfs_extents_tree {
    hfs_volume_t* vol;
    hfs_btree_t* btree;
} hfs_extents_tree_t;

/* Open the Extents Overflow B-Tree from a mounted volume */
hfs_extents_tree_t* hfs_extents_open(hfs_volume_t* vol);

/* Find extents for a given file ID, fork type, and logical block number */
int hfs_extents_find(hfs_extents_tree_t* ext_tree, uint32_t file_id, uint8_t fork_type,
                     uint32_t logical_block, HFSPlusExtentRecord out_record);

/* Close extents tree */
void hfs_extents_close(hfs_extents_tree_t* ext_tree);

#ifdef __cplusplus
}
#endif

#endif /* HFSPLUS_EXTENTS_H */
