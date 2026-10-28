#include "hfsplus/catalog.h"
#include "hfsplus/hfsplus_unicode.h"
#include "hfsplus/hfsplus_endian.h"
#include "hfsplus/mem.h"
#include <string.h>

static void parse_fork_to_internal(const HFSPlusForkData* src, hfs_fork_t* dst) {
    dst->logical_size = be64_to_cpu(src->logicalSize);
    dst->clump_size = be32_to_cpu(src->clumpSize);
    dst->total_blocks = be32_to_cpu(src->totalBlocks);
    for (int i = 0; i < 8; i++) {
        dst->extents[i].startBlock = be32_to_cpu(src->extents[i].startBlock);
        dst->extents[i].blockCount = be32_to_cpu(src->extents[i].blockCount);
    }
}

static void parse_catalog_record(const HFSPlusCatalogKey* key, const void* data, size_t data_len, hfs_dirent_t* out) {
    memset(out, 0, sizeof(hfs_dirent_t));

    if (key) {
        hfs_unistr_to_utf8(&key->nodeName, out->name, sizeof(out->name));
    }

    if (!data || data_len < 2) return;

    uint16_t rec_type = be16_to_cpu(*(const uint16_t*)data);

    if (rec_type == kHFSPlusFolderRecord) {
        const HFSPlusCatalogFolder* folder = (const HFSPlusCatalogFolder*)data;
        out->is_directory = true;
        out->cnid = be32_to_cpu(folder->folderID);
        out->create_date = be32_to_cpu(folder->createDate);
        out->modify_date = be32_to_cpu(folder->contentModDate);
        out->access_date = be32_to_cpu(folder->accessDate);
        out->posix_mode = be16_to_cpu(folder->bsdInfo.fileMode);
        out->uid = be32_to_cpu(folder->bsdInfo.ownerID);
        out->gid = be32_to_cpu(folder->bsdInfo.groupID);
    } else if (rec_type == kHFSPlusFileRecord) {
        const HFSPlusCatalogFile* file = (const HFSPlusCatalogFile*)data;
        out->is_directory = false;
        out->cnid = be32_to_cpu(file->fileID);
        out->create_date = be32_to_cpu(file->createDate);
        out->modify_date = be32_to_cpu(file->contentModDate);
        out->access_date = be32_to_cpu(file->accessDate);
        out->posix_mode = be16_to_cpu(file->bsdInfo.fileMode);
        out->uid = be32_to_cpu(file->bsdInfo.ownerID);
        out->gid = be32_to_cpu(file->bsdInfo.groupID);

        parse_fork_to_internal(&file->dataFork, &out->data_fork);
        parse_fork_to_internal(&file->resourceFork, &out->rsrc_fork);
        out->size = out->data_fork.logical_size;
    }
}

hfs_catalog_t* hfs_catalog_open(hfs_volume_t* vol) {
    if (!vol) return NULL;

    hfs_catalog_t* cat = (hfs_catalog_t*)hfs_zalloc(sizeof(hfs_catalog_t));
    if (!cat) return NULL;

    cat->vol = vol;
    cat->btree = hfs_btree_open(vol, &vol->catalog_file,
                                hfs_catalog_key_compare,
                                vol->is_hfsx);
    if (!cat->btree) {
        hfs_free(cat);
        return NULL;
    }

    return cat;
}

int hfs_catalog_lookup(hfs_catalog_t* cat, uint32_t parent_cnid, const char* name, hfs_dirent_t* out_dirent) {
    if (!cat || !cat->btree || !name || !out_dirent) return -1;

    HFSPlusCatalogKey search_key;
    memset(&search_key, 0, sizeof(search_key));
    search_key.parentID = cpu_to_be32(parent_cnid);
    hfs_utf8_to_unistr(name, &search_key.nodeName);

    uint16_t unistr_len = be16_to_cpu(search_key.nodeName.length);
    search_key.keyLength = cpu_to_be16((uint16_t)(sizeof(uint32_t) + sizeof(uint16_t) + unistr_len * sizeof(uint16_t)));

    void* found_key = NULL;
    void* found_data = NULL;
    size_t found_len = 0;

    int res = hfs_btree_search(cat->btree, &search_key, &found_key, &found_data, &found_len, NULL, NULL);
    if (res != 0) {
        return -1;
    }

    parse_catalog_record((const HFSPlusCatalogKey*)found_key, found_data, found_len, out_dirent);
    return 0;
}

int hfs_catalog_lookup_path(hfs_catalog_t* cat, const char* path, hfs_dirent_t* out_dirent) {
    if (!cat || !path || !out_dirent) return -1;

    /* Root directory request */
    if (strcmp(path, "/") == 0 || strcmp(path, "\\") == 0 || path[0] == '\0') {
        memset(out_dirent, 0, sizeof(hfs_dirent_t));
        out_dirent->cnid = kHFSRootFolderID;
        out_dirent->is_directory = true;
        out_dirent->create_date = cat->vol->create_date;
        out_dirent->modify_date = cat->vol->modify_date;
        strcpy(out_dirent->name, "/");
        return 0;
    }

    char path_buf[1024];
    strncpy(path_buf, path, sizeof(path_buf) - 1);
    path_buf[sizeof(path_buf) - 1] = '\0';

    uint32_t current_cnid = kHFSRootFolderID;
    char* token = strtok(path_buf, "/\\");

    hfs_dirent_t current_entry;
    memset(&current_entry, 0, sizeof(current_entry));
    current_entry.cnid = kHFSRootFolderID;
    current_entry.is_directory = true;

    while (token != NULL) {
        if (strlen(token) == 0 || strcmp(token, ".") == 0) {
            token = strtok(NULL, "/\\");
            continue;
        }

        if (!current_entry.is_directory) {
            return -1; /* Not a directory */
        }

        if (hfs_catalog_lookup(cat, current_cnid, token, &current_entry) != 0) {
            return -1; /* Component not found */
        }

        current_cnid = current_entry.cnid;
        token = strtok(NULL, "/\\");
    }

    *out_dirent = current_entry;
    return 0;
}

int hfs_catalog_iterate_dir(hfs_catalog_t* cat, uint32_t folder_cnid, hfs_dir_callback_fn callback, void* user_data) {
    if (!cat || !cat->btree || !callback) return -1;

    /* A folder thread record in Catalog has parentID = folder_cnid, nodeName.length = 0 */
    HFSPlusCatalogKey search_key;
    memset(&search_key, 0, sizeof(search_key));
    search_key.parentID = cpu_to_be32(folder_cnid);
    search_key.nodeName.length = 0;
    search_key.keyLength = cpu_to_be16((uint16_t)(sizeof(uint32_t) + sizeof(uint16_t)));

    uint32_t leaf_node = 0;
    uint16_t record_idx = 0;

    /* Search for start of records with this parentID */
    hfs_btree_search(cat->btree, &search_key, NULL, NULL, NULL, &leaf_node, &record_idx);
    if (leaf_node == 0) {
        /* Fallback: start from first leaf node */
        leaf_node = cat->btree->header.firstLeafNode;
        record_idx = 0;
    }

    uint8_t* node_buf = cat->btree->node_buffer;

    while (leaf_node != 0) {
        if (hfs_btree_read_node(cat->btree, leaf_node, node_buf) != 0) {
            return -1;
        }

        const BTNodeDescriptor* desc = (const BTNodeDescriptor*)node_buf;
        uint16_t num_records = be16_to_cpu(desc->numRecords);

        for (uint16_t i = record_idx; i < num_records; i++) {
            void* key_ptr = NULL;
            void* data_ptr = NULL;
            size_t data_len = 0;

            if (hfs_btree_get_node_record(cat->btree, node_buf, i, &key_ptr, &data_ptr, &data_len) != 0) {
                continue;
            }

            const HFSPlusCatalogKey* cat_key = (const HFSPlusCatalogKey*)key_ptr;
            uint32_t key_parent = be32_to_cpu(cat_key->parentID);

            if (key_parent < folder_cnid) {
                continue;
            }
            if (key_parent > folder_cnid) {
                /* Finished all children of this folder */
                return 0;
            }

            uint16_t rec_type = be16_to_cpu(*(const uint16_t*)data_ptr);
            /* Only report folders and files, skip thread records */
            if (rec_type == kHFSPlusFolderRecord || rec_type == kHFSPlusFileRecord) {
                hfs_dirent_t entry;
                parse_catalog_record(cat_key, data_ptr, data_len, &entry);
                if (callback(&entry, user_data) != 0) {
                    return 0; /* Callback requested stop */
                }
            }
        }

        leaf_node = be32_to_cpu(desc->fLink);
        record_idx = 0;
    }

    return 0;
}

void hfs_catalog_close(hfs_catalog_t* cat) {
    if (!cat) return;
    if (cat->btree) {
        hfs_btree_close(cat->btree);
    }
    hfs_free(cat);
}
