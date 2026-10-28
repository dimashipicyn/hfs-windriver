#include "hfsplus/volume.h"
#include "hfsplus/hfsplus_endian.h"
#include "hfsplus/mem.h"

static void parse_fork(const HFSPlusForkData* src, hfs_fork_t* dst) {
    dst->logical_size = be64_to_cpu(src->logicalSize);
    dst->clump_size = be32_to_cpu(src->clumpSize);
    dst->total_blocks = be32_to_cpu(src->totalBlocks);

    for (int i = 0; i < 8; i++) {
        dst->extents[i].startBlock = be32_to_cpu(src->extents[i].startBlock);
        dst->extents[i].blockCount = be32_to_cpu(src->extents[i].blockCount);
    }
}

static bool parse_header(const HFSPlusVolumeHeader* raw, hfs_volume_t* vol) {
    uint16_t sig = be16_to_cpu(raw->signature);
    uint16_t ver = be16_to_cpu(raw->version);

    if (sig != kHFSPlusSigWord && sig != kHFSXSigWord) {
        return false;
    }

    if (ver != kHFSPlusVersion) {
        return false;
    }

    uint32_t block_size = be32_to_cpu(raw->blockSize);
    /* Block size must be a power of 2 and at least 512 bytes */
    if (block_size < 512 || (block_size & (block_size - 1)) != 0) {
        return false;
    }

    vol->signature = sig;
    vol->version = ver;
    vol->attributes = be32_to_cpu(raw->attributes);
    vol->block_size = block_size;
    vol->total_blocks = be32_to_cpu(raw->totalBlocks);
    vol->free_blocks = be32_to_cpu(raw->freeBlocks);
    vol->next_allocation = be32_to_cpu(raw->nextAllocation);
    vol->next_catalog_id = be32_to_cpu(raw->nextCatalogID);
    vol->file_count = be32_to_cpu(raw->fileCount);
    vol->folder_count = be32_to_cpu(raw->folderCount);
    vol->create_date = be32_to_cpu(raw->createDate);
    vol->modify_date = be32_to_cpu(raw->modifyDate);
    vol->backup_date = be32_to_cpu(raw->backupDate);
    vol->checked_date = be32_to_cpu(raw->checkedDate);

    vol->is_hfsx = (sig == kHFSXSigWord);
    vol->is_journaled = (vol->attributes & (1 << 13)) != 0;

    parse_fork(&raw->allocationFile, &vol->allocation_file);
    parse_fork(&raw->extentsFile, &vol->extents_file);
    parse_fork(&raw->catalogFile, &vol->catalog_file);
    parse_fork(&raw->attributesFile, &vol->attributes_file);
    parse_fork(&raw->startupFile, &vol->startup_file);

    return true;
}

hfs_volume_t* hfs_volume_open(hfs_block_device_t* dev) {
    if (!dev) return NULL;

    hfs_volume_t* vol = (hfs_volume_t*)hfs_zalloc(sizeof(hfs_volume_t));
    if (!vol) return NULL;

    vol->dev = dev;

    /* Read primary volume header at offset 1024 bytes */
    HFSPlusVolumeHeader raw_header;
    if (hfs_block_device_read(dev, 1024, &raw_header, sizeof(raw_header)) == 0) {
        if (parse_header(&raw_header, vol)) {
            vol->mounted_from_backup_header = false;
            return vol;
        }
    }

    /* Primary header failed, try alternate volume header (1024 bytes before end of device) */
    uint64_t dev_size = dev->ops->get_size(dev);
    if (dev_size >= 2048) {
        uint64_t backup_offset = dev_size - 1024;
        if (hfs_block_device_read(dev, backup_offset, &raw_header, sizeof(raw_header)) == 0) {
            if (parse_header(&raw_header, vol)) {
                vol->mounted_from_backup_header = true;
                return vol;
            }
        }
    }

    hfs_free(vol);
    return NULL;
}

int hfs_volume_read_blocks(hfs_volume_t* vol, uint32_t start_block, uint32_t block_count, void* buffer) {
    if (!vol || !vol->dev || !buffer) return -1;
    if (block_count == 0) return 0;

    if (start_block + block_count > vol->total_blocks) {
        return -1;
    }

    uint64_t offset = hfs_volume_block_to_offset(vol, start_block);
    size_t bytes = (size_t)block_count * vol->block_size;

    return hfs_block_device_read(vol->dev, offset, buffer, bytes);
}

void hfs_volume_close(hfs_volume_t* vol) {
    if (!vol) return;
    hfs_free(vol);
}
