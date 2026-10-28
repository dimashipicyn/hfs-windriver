#include "hfsplus/extents.h"
#include "hfsplus/hfsplus_unicode.h"
#include "hfsplus/hfsplus_endian.h"
#include "hfsplus/mem.h"
#include <string.h>

hfs_extents_tree_t* hfs_extents_open(hfs_volume_t* vol) {
    if (!vol) return NULL;
    if (vol->extents_file.logical_size < 512) return NULL;

    hfs_extents_tree_t* tree = (hfs_extents_tree_t*)hfs_zalloc(sizeof(hfs_extents_tree_t));
    if (!tree) return NULL;

    tree->vol = vol;
    tree->btree = hfs_btree_open(vol, &vol->extents_file,
                                hfs_extent_key_compare,
                                false);
    if (!tree->btree) {
        hfs_free(tree);
        return NULL;
    }

    return tree;
}

int hfs_extents_find(hfs_extents_tree_t* ext_tree, uint32_t file_id, uint8_t fork_type,
                     uint32_t logical_block, HFSPlusExtentRecord out_record) {
    if (!ext_tree || !ext_tree->btree || !out_record) return -1;

    HFSPlusExtentKey key;
    memset(&key, 0, sizeof(key));
    key.keyLength = cpu_to_be16(sizeof(HFSPlusExtentKey) - sizeof(uint16_t));
    key.forkType = fork_type;
    key.fileID = cpu_to_be32(file_id);
    key.startBlock = cpu_to_be32(logical_block);

    void* found_key = NULL;
    void* found_data = NULL;
    size_t found_len = 0;

    int res = hfs_btree_search(ext_tree->btree, &key, &found_key, &found_data, &found_len, NULL, NULL);
    if (res != 0 || !found_data || found_len < sizeof(HFSPlusExtentRecord)) {
        return -1;
    }

    const HFSPlusExtentDescriptor* raw_desc = (const HFSPlusExtentDescriptor*)found_data;
    for (int i = 0; i < 8; i++) {
        out_record[i].startBlock = be32_to_cpu(raw_desc[i].startBlock);
        out_record[i].blockCount = be32_to_cpu(raw_desc[i].blockCount);
    }

    return 0;
}

void hfs_extents_close(hfs_extents_tree_t* ext_tree) {
    if (!ext_tree) return;
    if (ext_tree->btree) {
        hfs_btree_close(ext_tree->btree);
    }
    hfs_free(ext_tree);
}
