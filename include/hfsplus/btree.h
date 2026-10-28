#ifndef HFSPLUS_BTREE_H
#define HFSPLUS_BTREE_H

#include "hfsplus/volume.h"
#include "hfsplus/hfsplus_defs.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef int (*hfs_btree_key_compare_fn)(const void* key1, const void* key2, bool case_sensitive);

typedef struct hfs_btree {
    hfs_volume_t* vol;
    hfs_fork_t fork;
    BTHeaderRec header;
    uint32_t node_size;
    bool case_sensitive;
    hfs_btree_key_compare_fn key_compare;

    /* Reusable node buffer */
    uint8_t* node_buffer;
} hfs_btree_t;

/* Open a B-Tree from a volume fork (e.g. catalogFile or extentsFile) */
hfs_btree_t* hfs_btree_open(hfs_volume_t* vol, const hfs_fork_t* fork, hfs_btree_key_compare_fn key_compare, bool case_sensitive);

/* Read a node into a provided buffer (must be at least btree->node_size) */
int hfs_btree_read_node(hfs_btree_t* btree, uint32_t node_index, uint8_t* buffer);

/* Get record pointer and length inside a loaded node buffer */
int hfs_btree_get_node_record(const hfs_btree_t* btree, const uint8_t* node_buf, uint16_t record_idx,
                              void** out_key, void** out_data, size_t* out_data_len);

/* Search the B-Tree for search_key.
 * If found, copies node into btree->node_buffer, sets out_key, out_data, out_data_len.
 * Returns 0 on exact match, 1 if found nearest predecessor/position, negative on error. */
int hfs_btree_search(hfs_btree_t* btree, const void* search_key,
                     void** out_key, void** out_data, size_t* out_data_len,
                     uint32_t* out_leaf_node, uint16_t* out_record_idx);

/* Close B-Tree and free resources */
void hfs_btree_close(hfs_btree_t* btree);

#ifdef __cplusplus
}
#endif

#endif /* HFSPLUS_BTREE_H */
