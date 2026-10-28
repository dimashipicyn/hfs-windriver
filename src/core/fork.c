#include "hfsplus/fork.h"
#include <string.h>

int hfs_fork_read(hfs_volume_t* vol, const hfs_fork_t* fork, uint64_t offset, void* buffer, size_t count) {
    if (!vol || !fork || !buffer) return -1;
    if (count == 0) return 0;

    if (offset >= fork->logical_size) {
        return -1;
    }

    if (offset + count > fork->logical_size) {
        /* Clamp count to available size */
        count = (size_t)(fork->logical_size - offset);
    }

    uint32_t block_size = vol->block_size;
    if (block_size == 0) return -1;

    uint8_t* dst = (uint8_t*)buffer;
    size_t bytes_read = 0;

    while (count > 0) {
        uint32_t logical_block = (uint32_t)(offset / block_size);
        uint32_t block_offset = (uint32_t)(offset % block_size);
        size_t chunk = block_size - block_offset;
        if (chunk > count) {
            chunk = count;
        }

        /* Search in initial 8 extents */
        uint32_t current_block_base = 0;
        bool found = false;
        uint32_t phys_block = 0;

        for (int i = 0; i < 8; i++) {
            uint32_t ext_start = fork->extents[i].startBlock;
            uint32_t ext_count = fork->extents[i].blockCount;
            if (ext_count == 0) break;

            if (logical_block >= current_block_base &&
                logical_block < current_block_base + ext_count) {
                phys_block = ext_start + (logical_block - current_block_base);
                found = true;
                break;
            }

            current_block_base += ext_count;
        }

        if (found) {
            uint64_t phys_offset = hfs_volume_block_to_offset(vol, phys_block) + block_offset;
            if (hfs_block_device_read(vol->dev, phys_offset, dst + bytes_read, chunk) != 0) {
                return -1;
            }
        } else {
            /* Sparse block or overflow extent not yet loaded: zero-fill for now */
            memset(dst + bytes_read, 0, chunk);
        }

        offset += chunk;
        bytes_read += chunk;
        count -= chunk;
    }

    return 0;
}
