#include "hfsplus/volume.h"
#include "hfsplus/hfsplus_endian.h"
#include "hfsplus/mem.h"
#include <string.h>

#pragma pack(push, 1)
typedef struct {
    uint8_t  signature[8];        /* "EFI PART" */
    uint32_t revision;
    uint32_t header_size;
    uint32_t header_crc32;
    uint32_t reserved;
    uint64_t my_lba;
    uint64_t alternate_lba;
    uint64_t first_usable_lba;
    uint64_t last_usable_lba;
    uint8_t  disk_guid[16];
    uint64_t partition_entry_lba;
    uint32_t num_partition_entries;
    uint32_t sizeof_partition_entry;
    uint32_t partition_entry_crc32;
} gpt_header_t;

typedef struct {
    uint8_t  type_guid[16];
    uint8_t  unique_guid[16];
    uint64_t starting_lba;
    uint64_t ending_lba;
    uint64_t attributes;
    uint16_t name[36];
} gpt_entry_t;
#pragma pack(pop)

/* Apple HFS+ Partition Type GUID: 48465300-0000-11AA-AA11-00306543ECAC */
static const uint8_t kAppleHFSPlusGUID[16] = {
    0x00, 0x53, 0x46, 0x48, 0x00, 0x00, 0xAA, 0x11,
    0xAA, 0x11, 0x00, 0x30, 0x65, 0x43, 0xEC, 0xAC
};

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

static bool probe_hfs_at_offset(hfs_block_device_t* dev, uint64_t offset, hfs_volume_t* vol) {
    HFSPlusVolumeHeader raw_header;
    if (hfs_block_device_read(dev, offset + 1024, &raw_header, sizeof(raw_header)) != 0) {
        return false;
    }

    if (!parse_header(&raw_header, vol)) {
        return false;
    }

    vol->partition_offset = offset;
    return true;
}

static bool scan_gpt_partitions(hfs_block_device_t* dev, hfs_volume_t* vol) {
    /* Read GPT Header at LBA 1 (offset 512) */
    gpt_header_t gpt;
    if (hfs_block_device_read(dev, 512, &gpt, sizeof(gpt)) != 0) {
        return false;
    }

    if (memcmp(gpt.signature, "EFI PART", 8) != 0) {
        return false;
    }

    uint64_t entry_lba = gpt.partition_entry_lba;
    uint32_t num_entries = gpt.num_partition_entries;
    uint32_t entry_size = gpt.sizeof_partition_entry;

    if (num_entries > 128) num_entries = 128;
    if (entry_size < sizeof(gpt_entry_t)) return false;

    uint8_t entry_buf[512];
    for (uint32_t i = 0; i < num_entries; i++) {
        uint64_t entry_offset = entry_lba * 512 + (uint64_t)i * entry_size;
        if (hfs_block_device_read(dev, entry_offset, entry_buf, sizeof(gpt_entry_t)) != 0) {
            break;
        }

        const gpt_entry_t* entry = (const gpt_entry_t*)entry_buf;
        if (entry->starting_lba == 0 || entry->ending_lba < entry->starting_lba) {
            continue;
        }

        uint64_t part_offset = entry->starting_lba * 512;
        uint64_t part_size = (entry->ending_lba - entry->starting_lba + 1) * 512;

        /* Check if GUID matches Apple HFS+ or if volume header is present at partition offset */
        if (memcmp(entry->type_guid, kAppleHFSPlusGUID, 16) == 0 ||
            probe_hfs_at_offset(dev, part_offset, vol)) {
            if (probe_hfs_at_offset(dev, part_offset, vol)) {
                vol->partition_size = part_size;
                return true;
            }
        }
    }

    return false;
}

static bool scan_mbr_partitions(hfs_block_device_t* dev, hfs_volume_t* vol) {
    uint8_t sector0[512];
    if (hfs_block_device_read(dev, 0, sector0, sizeof(sector0)) != 0) {
        return false;
    }

    if (sector0[510] != 0x55 || sector0[511] != 0xAA) {
        return false;
    }

    for (int i = 0; i < 4; i++) {
        const uint8_t* entry = sector0 + 446 + i * 16;
        uint8_t part_type = entry[4];
        if (part_type == 0) continue;

        uint32_t start_lba = (uint32_t)entry[8] | ((uint32_t)entry[9] << 8) |
                             ((uint32_t)entry[10] << 16) | ((uint32_t)entry[11] << 24);
        uint32_t sector_count = (uint32_t)entry[12] | ((uint32_t)entry[13] << 8) |
                               ((uint32_t)entry[14] << 16) | ((uint32_t)entry[15] << 24);

        if (start_lba > 0) {
            uint64_t part_offset = (uint64_t)start_lba * 512;
            if (probe_hfs_at_offset(dev, part_offset, vol)) {
                vol->partition_size = (uint64_t)sector_count * 512;
                return true;
            }
        }
    }

    return false;
}

hfs_volume_t* hfs_volume_open(hfs_block_device_t* dev) {
    if (!dev) return NULL;

    hfs_volume_t* vol = (hfs_volume_t*)hfs_zalloc(sizeof(hfs_volume_t));
    if (!vol) return NULL;

    vol->dev = dev;

    /* 1. Try raw volume / disk image at offset 0 */
    if (probe_hfs_at_offset(dev, 0, vol)) {
        vol->mounted_from_backup_header = false;
        return vol;
    }

    /* 2. Try GPT partition table (standard on Mac USB disks) */
    if (scan_gpt_partitions(dev, vol)) {
        vol->mounted_from_backup_header = false;
        return vol;
    }

    /* 3. Try MBR partition table */
    if (scan_mbr_partitions(dev, vol)) {
        vol->mounted_from_backup_header = false;
        return vol;
    }

    /* 4. If primary headers failed, try backup volume header */
    uint64_t dev_size = dev->ops->get_size(dev);
    uint64_t vol_size = vol->partition_size ? vol->partition_size : dev_size;
    if (vol_size >= 2048) {
        uint64_t backup_offset = vol->partition_offset + vol_size - 1024;
        HFSPlusVolumeHeader raw_header;
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
