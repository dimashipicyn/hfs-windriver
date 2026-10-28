#include "hfsplus/btree.h"
#include "hfsplus/fork.h"
#include "hfsplus/hfsplus_endian.h"
#include "hfsplus/mem.h"
#include <string.h>

hfs_btree_t* hfs_btree_open(hfs_volume_t* vol, const hfs_fork_t* fork, hfs_btree_key_compare_fn key_compare, bool case_sensitive) {
    if (!vol || !fork || !key_compare) return NULL;
    if (fork->logical_size < 512) return NULL;

    hfs_btree_t* btree = (hfs_btree_t*)hfs_zalloc(sizeof(hfs_btree_t));
    if (!btree) return NULL;

    btree->vol = vol;
    btree->fork = *fork;
    btree->key_compare = key_compare;
    btree->case_sensitive = case_sensitive;

    /* First, read the first 512 bytes of Node 0 to determine the true nodeSize from BTHeaderRec */
    uint8_t header_buf[512];
    if (hfs_fork_read(vol, fork, 0, header_buf, sizeof(header_buf)) != 0) {
        hfs_free(btree);
        return NULL;
    }

    const BTNodeDescriptor* node_desc = (const BTNodeDescriptor*)header_buf;
    if (node_desc->kind != kBTHeaderNode) {
        hfs_free(btree);
        return NULL;
    }

    /* BTHeaderRec is record 0 in Header Node (starts at byte 14) */
    const BTHeaderRec* raw_hdr = (const BTHeaderRec*)(header_buf + sizeof(BTNodeDescriptor));

    btree->node_size = be16_to_cpu(raw_hdr->nodeSize);
    if (btree->node_size < 512 || (btree->node_size & (btree->node_size - 1)) != 0) {
        hfs_free(btree);
        return NULL;
    }

    btree->header.treeDepth = be16_to_cpu(raw_hdr->treeDepth);
    btree->header.rootNode = be32_to_cpu(raw_hdr->rootNode);
    btree->header.leafRecords = be32_to_cpu(raw_hdr->leafRecords);
    btree->header.firstLeafNode = be32_to_cpu(raw_hdr->firstLeafNode);
    btree->header.lastLeafNode = be32_to_cpu(raw_hdr->lastLeafNode);
    btree->header.nodeSize = btree->node_size;
    btree->header.maxKeyLength = be16_to_cpu(raw_hdr->maxKeyLength);
    btree->header.totalNodes = be32_to_cpu(raw_hdr->totalNodes);
    btree->header.freeNodes = be32_to_cpu(raw_hdr->freeNodes);
    btree->header.clumpSize = be32_to_cpu(raw_hdr->clumpSize);
    btree->header.btreeType = raw_hdr->btreeType;
    btree->header.keyCompareType = raw_hdr->keyCompareType;
    btree->header.attributes = be32_to_cpu(raw_hdr->attributes);

    btree->node_buffer = (uint8_t*)hfs_alloc(btree->node_size);
    if (!btree->node_buffer) {
        hfs_free(btree);
        return NULL;
    }

    return btree;
}

int hfs_btree_read_node(hfs_btree_t* btree, uint32_t node_index, uint8_t* buffer) {
    if (!btree || !buffer) return -1;
    uint64_t offset = (uint64_t)node_index * (uint64_t)btree->node_size;
    return hfs_fork_read(btree->vol, &btree->fork, offset, buffer, btree->node_size);
}

int hfs_btree_get_node_record(const hfs_btree_t* btree, const uint8_t* node_buf, uint16_t record_idx,
                              void** out_key, void** out_data, size_t* out_data_len) {
    if (!btree || !node_buf) return -1;

    const BTNodeDescriptor* desc = (const BTNodeDescriptor*)node_buf;
    uint16_t num_records = be16_to_cpu(desc->numRecords);
    if (record_idx >= num_records) {
        return -1;
    }

    /* Record offsets table is at the end of the node */
    uint32_t ns = btree->node_size;
    const uint16_t* off_ptr = (const uint16_t*)(node_buf + ns - 2 * (record_idx + 1));
    const uint16_t* next_off_ptr = (const uint16_t*)(node_buf + ns - 2 * (record_idx + 2));

    uint16_t rec_offset = be16_to_cpu(*off_ptr);
    uint16_t next_offset = be16_to_cpu(*next_off_ptr);

    if (rec_offset >= ns || next_offset > ns || next_offset < rec_offset) {
        return -1;
    }

    size_t rec_len = next_offset - rec_offset;
    const uint8_t* rec_ptr = node_buf + rec_offset;

    /* In HFS+, the first 2 bytes of any key is keyLength */
    uint16_t key_len = be16_to_cpu(*(const uint16_t*)rec_ptr);
    size_t total_key_size = 2 + key_len;
    if (total_key_size > rec_len) {
        return -1;
    }

    if (out_key) {
        *out_key = (void*)rec_ptr;
    }

    if (out_data) {
        *out_data = (void*)(rec_ptr + total_key_size);
    }

    if (out_data_len) {
        *out_data_len = rec_len - total_key_size;
    }

    return 0;
}

int hfs_btree_search(hfs_btree_t* btree, const void* search_key,
                     void** out_key, void** out_data, size_t* out_data_len,
                     uint32_t* out_leaf_node, uint16_t* out_record_idx) {
    if (!btree || !search_key || !btree->node_buffer) return -1;
    if (btree->header.rootNode == 0) return -1; /* Empty tree */

    uint32_t current_node = btree->header.rootNode;
    uint8_t* buf = btree->node_buffer;

    while (1) {
        if (hfs_btree_read_node(btree, current_node, buf) != 0) {
            return -1;
        }

        const BTNodeDescriptor* desc = (const BTNodeDescriptor*)buf;
        uint16_t num_records = be16_to_cpu(desc->numRecords);
        if (num_records == 0) {
            return -1;
        }

        if (desc->kind == kBTIndexNode) {
            /* Find record with greatest key <= search_key */
            uint16_t best_rec = 0;
            int low = 0;
            int high = num_records - 1;

            while (low <= high) {
                int mid = low + (high - low) / 2;
                void* k = NULL;
                if (hfs_btree_get_node_record(btree, buf, (uint16_t)mid, &k, NULL, NULL) != 0) {
                    return -1;
                }

                int cmp = btree->key_compare(k, search_key, btree->case_sensitive);
                if (cmp <= 0) {
                    best_rec = (uint16_t)mid;
                    low = mid + 1; /* Try to find a larger key that is still <= search_key */
                } else {
                    high = mid - 1;
                }
            }

            void* child_ptr = NULL;
            size_t child_len = 0;
            if (hfs_btree_get_node_record(btree, buf, best_rec, NULL, &child_ptr, &child_len) != 0 || child_len < 4) {
                return -1;
            }

            current_node = be32_to_cpu(*(const uint32_t*)child_ptr);
        } else if (desc->kind == kBTLeafNode) {
            /* Binary search within leaf node for exact match */
            int low = 0;
            int high = num_records - 1;

            while (low <= high) {
                int mid = low + (high - low) / 2;
                void* k = NULL;
                void* d = NULL;
                size_t d_len = 0;

                if (hfs_btree_get_node_record(btree, buf, (uint16_t)mid, &k, &d, &d_len) != 0) {
                    return -1;
                }

                int cmp = btree->key_compare(k, search_key, btree->case_sensitive);
                if (cmp == 0) {
                    /* Exact match! */
                    if (out_key) *out_key = k;
                    if (out_data) *out_data = d;
                    if (out_data_len) *out_data_len = d_len;
                    if (out_leaf_node) *out_leaf_node = current_node;
                    if (out_record_idx) *out_record_idx = (uint16_t)mid;
                    return 0;
                } else if (cmp < 0) {
                    low = mid + 1;
                } else {
                    high = mid - 1;
                }
            }

            /* Not exact match in leaf, but record insertion/start position */
            if (out_leaf_node) *out_leaf_node = current_node;
            if (out_record_idx) *out_record_idx = (uint16_t)low;
            return -1;
        } else {
            /* Unexpected node kind */
            return -1;
        }
    }
}

void hfs_btree_close(hfs_btree_t* btree) {
    if (!btree) return;
    if (btree->node_buffer) {
        hfs_free(btree->node_buffer);
    }
    hfs_free(btree);
}
