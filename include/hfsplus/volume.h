#ifndef HFSPLUS_VOLUME_H
#define HFSPLUS_VOLUME_H

#include "hfsplus/hfsplus_defs.h"
#include "hfsplus/block_device.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Parsed in-memory representation of an HFS+ Fork */
typedef struct hfs_fork {
    uint64_t logical_size;
    uint32_t clump_size;
    uint32_t total_blocks;
    HFSPlusExtentRecord extents; /* Extents converted to CPU endianness */
} hfs_fork_t;

/* In-memory mounted HFS+ Volume */
typedef struct hfs_volume {
    hfs_block_device_t* dev;

    /* Volume parameters (Host CPU endian) */
    uint16_t signature;
    uint16_t version;
    uint32_t attributes;
    uint32_t block_size;
    uint32_t total_blocks;
    uint32_t free_blocks;
    uint32_t next_allocation;
    uint32_t next_catalog_id;
    uint32_t file_count;
    uint32_t folder_count;
    uint32_t create_date;
    uint32_t modify_date;
    uint32_t backup_date;
    uint32_t checked_date;

    /* Special files / forks */
    hfs_fork_t allocation_file;
    hfs_fork_t extents_file;
    hfs_fork_t catalog_file;
    hfs_fork_t attributes_file;
    hfs_fork_t startup_file;

    /* Flags */
    bool is_hfsx;
    bool is_journaled;
    bool mounted_from_backup_header;

    /* Partition bounds (for physical drives with GPT / MBR) */
    uint64_t partition_offset;
    uint64_t partition_size;
} hfs_volume_t;

/* Mount an HFS+ volume from an opened block device. Returns NULL if invalid volume. */
hfs_volume_t* hfs_volume_open(hfs_block_device_t* dev);

/* Read one or more allocation blocks from volume */
int hfs_volume_read_blocks(hfs_volume_t* vol, uint32_t start_block, uint32_t block_count, void* buffer);

/* Read a single allocation block */
static inline int hfs_volume_read_block(hfs_volume_t* vol, uint32_t block_index, void* buffer) {
    return hfs_volume_read_blocks(vol, block_index, 1, buffer);
}

/* Calculate byte offset on the block device for a given allocation block index */
static inline uint64_t hfs_volume_block_to_offset(const hfs_volume_t* vol, uint32_t block_index) {
    return vol->partition_offset + (uint64_t)block_index * (uint64_t)vol->block_size;
}

/* Close volume and free in-memory structure (does not close the underlying dev) */
void hfs_volume_close(hfs_volume_t* vol);

#ifdef __cplusplus
}
#endif

#endif /* HFSPLUS_VOLUME_H */
