#include "hfsplus/diag.h"
#include "hfsplus/volume.h"
#include "hfsplus/btree.h"
#include "hfsplus/catalog.h"
#include "hfsplus/hfsplus_endian.h"
#include "hfsplus/hfsplus_unicode.h"
#include "hfsplus/mem.h"
#include <stdio.h>
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
} diag_gpt_header_t;

typedef struct {
    uint8_t  type_guid[16];
    uint8_t  unique_guid[16];
    uint64_t starting_lba;
    uint64_t ending_lba;
    uint64_t attributes;
    uint16_t name[36];
} diag_gpt_entry_t;
#pragma pack(pop)

/* Known GUIDs */
static const uint8_t kGUID_HFSPlus[16] = {
    0x00, 0x53, 0x46, 0x48, 0x00, 0x00, 0xAA, 0x11,
    0xAA, 0x11, 0x00, 0x30, 0x65, 0x43, 0xEC, 0xAC
};

static const uint8_t kGUID_APFS[16] = {
    0xEF, 0x57, 0x34, 0x7C, 0x00, 0x00, 0xAA, 0x11,
    0xAA, 0x11, 0x00, 0x30, 0x65, 0x43, 0xEC, 0xAC
};

static const uint8_t kGUID_EFI[16] = {
    0x28, 0x73, 0x2A, 0xC1, 0x1F, 0xF8, 0xD2, 0x11,
    0xBA, 0x4B, 0x00, 0xA0, 0xC9, 0x3E, 0xC9, 0x3B
};

static const char* identify_guid(const uint8_t guid[16]) {
    if (memcmp(guid, kGUID_HFSPlus, 16) == 0) return "Apple HFS+ (Mac OS Extended)";
    if (memcmp(guid, kGUID_APFS, 16) == 0)    return "Apple APFS (Apple File System)";
    if (memcmp(guid, kGUID_EFI, 16) == 0)     return "EFI System Partition";
    return "Unknown Partition Type";
}

static void print_guid_str(const uint8_t g[16]) {
    printf("%02X%02X%02X%02X-%02X%02X-%02X%02X-%02X%02X-%02X%02X%02X%02X%02X%02X",
           g[3], g[2], g[1], g[0], g[5], g[4], g[7], g[6],
           g[8], g[9], g[10], g[11], g[12], g[13], g[14], g[15]);
}

int hfs_run_diagnostics(hfs_block_device_t* dev) {
    if (!dev) {
        printf("[ERROR] Block device handle is NULL.\n");
        return -1;
    }

    uint64_t dev_size = dev->ops->get_size(dev);
    double size_gb = (double)dev_size / (1024.0 * 1024.0 * 1024.0);

    printf("\n=======================================================\n");
    printf("         HFS+ DRIVER HARDWARE & VOLUME DIAGNOSTICS      \n");
    printf("=======================================================\n");
    printf("Device Capacity : %llu bytes (%.2f GB)\n\n", (unsigned long long)dev_size, size_gb);

    /* 1. MBR Sector Analysis */
    printf("[1/4] Inspecting Master Boot Record (LBA 0)...\n");
    uint8_t sec0[512];
    if (hfs_block_device_read(dev, 0, sec0, sizeof(sec0)) != 0) {
        printf("  [FAIL] Failed to read Sector 0!\n");
        return -1;
    }

    uint16_t mbr_sig = (uint16_t)sec0[510] | ((uint16_t)sec0[511] << 8);
    printf("  MBR Signature: 0x%04X %s\n", mbr_sig, (mbr_sig == 0xAA55) ? "(Valid)" : "(Invalid)");

    for (int i = 0; i < 4; i++) {
        const uint8_t* p = sec0 + 446 + i * 16;
        uint8_t type = p[4];
        if (type != 0) {
            uint32_t start = (uint32_t)p[8] | ((uint32_t)p[9] << 8) | ((uint32_t)p[10] << 16) | ((uint32_t)p[11] << 24);
            uint32_t count = (uint32_t)p[12] | ((uint32_t)p[13] << 8) | ((uint32_t)p[14] << 16) | ((uint32_t)p[15] << 24);
            printf("  MBR Slot %d: Type 0x%02X (%s), LBA %u..%u (%.2f GB)\n",
                   i + 1, type, (type == 0xEE) ? "GPT Protective MBR" : "Standard MBR",
                   start, start + count, (double)((uint64_t)count * 512) / (1024.0 * 1024.0 * 1024.0));
        }
    }

    /* 2. GPT Analysis */
    printf("\n[2/4] Inspecting GUID Partition Table (LBA 1)...\n");
    diag_gpt_header_t gpt;
    bool has_gpt = false;
    uint64_t target_part_offset = 0;
    uint64_t target_part_size = 0;
    (void)target_part_size;
    bool found_hfs = false;
    bool found_apfs = false;

    if (hfs_block_device_read(dev, 512, &gpt, sizeof(gpt)) == 0 &&
        memcmp(gpt.signature, "EFI PART", 8) == 0) {
        has_gpt = true;
        printf("  GPT Header: Valid ('EFI PART')\n");
        printf("  Number of Partition Entries: %u (size=%u bytes)\n",
               gpt.num_partition_entries, gpt.sizeof_partition_entry);

        uint32_t count = gpt.num_partition_entries;
        if (count > 32) count = 32;

        uint8_t entry_buf[512];
        for (uint32_t i = 0; i < count; i++) {
            uint64_t entry_off = gpt.partition_entry_lba * 512 + (uint64_t)i * gpt.sizeof_partition_entry;
            if (hfs_block_device_read(dev, entry_off, entry_buf, sizeof(diag_gpt_entry_t)) != 0) break;

            const diag_gpt_entry_t* e = (const diag_gpt_entry_t*)entry_buf;
            if (e->starting_lba == 0 || e->ending_lba < e->starting_lba) continue;

            uint64_t p_size = (e->ending_lba - e->starting_lba + 1) * 512;
            printf("\n  --> Partition %u:\n", i + 1);
            printf("      Range : LBA %llu .. %llu (%.2f GB)\n",
                   (unsigned long long)e->starting_lba, (unsigned long long)e->ending_lba,
                   (double)p_size / (1024.0 * 1024.0 * 1024.0));
            printf("      GUID  : ");
            print_guid_str(e->type_guid);
            printf(" [%s]\n", identify_guid(e->type_guid));

            if (memcmp(e->type_guid, kGUID_APFS, 16) == 0) {
                found_apfs = true;
                printf("      [!] WARNING: This is an APFS partition! macOS formats modern drives with APFS.\n");
            }

            /* Probe for HFS+ Volume Header */
            uint64_t p_off = e->starting_lba * 512;
            HFSPlusVolumeHeader vhdr;
            if (hfs_block_device_read(dev, p_off + 1024, &vhdr, sizeof(vhdr)) == 0) {
                uint16_t sig = be16_to_cpu(vhdr.signature);
                uint16_t ver = be16_to_cpu(vhdr.version);
                if ((sig == kHFSPlusSigWord || sig == kHFSXSigWord) && ver == kHFSPlusVersion) {
                    printf("      [+] FOUND HFS+ VOLUME HEADER ON PARTITION %u!\n", i + 1);
                    found_hfs = true;
                    target_part_offset = p_off;
                    target_part_size = p_size;
                }
            }
        }
    } else {
        printf("  No GPT header at LBA 1.\n");
    }

    if (!has_gpt) {
        /* Check raw disk */
        HFSPlusVolumeHeader vhdr;
        if (hfs_block_device_read(dev, 1024, &vhdr, sizeof(vhdr)) == 0) {
            uint16_t sig = be16_to_cpu(vhdr.signature);
            uint16_t ver = be16_to_cpu(vhdr.version);
            if ((sig == kHFSPlusSigWord || sig == kHFSXSigWord) && ver == kHFSPlusVersion) {
                printf("  [+] FOUND HFS+ VOLUME HEADER AT RAW DISK OFFSET 0!\n");
                found_hfs = true;
                target_part_offset = 0;
                target_part_size = dev_size;
            }
        }
    }

    if (!found_hfs) {
        printf("\n=======================================================\n");
        printf("[RESULT] NO HFS+ FILESYSTEM FOUND ON THIS DEVICE!\n");
        if (found_apfs) {
            printf("\n--> CAUSE IDENTIFIED: The disk is formatted with APFS (Apple File System),\n");
            printf("    not HFS+ (Mac OS Extended)!\n");
            printf("    Modern macOS (High Sierra through Sequoia) formats USB drives as APFS by default.\n");
        } else {
            printf("\n--> CAUSE IDENTIFIED: The drive does not contain an HFS+ superblock.\n");
            printf("    Check in Disk Management if this is the correct PhysicalDrive number.\n");
        }
        printf("=======================================================\n");
        return 0;
    }

    /* 3. Volume Header Analysis */
    printf("\n[3/4] Parsing HFS+ Volume Header at offset %llu...\n", (unsigned long long)target_part_offset);
    hfs_volume_t* vol = hfs_volume_open(dev);
    if (!vol) {
        printf("  [FAIL] Failed to initialize volume structures!\n");
        return -1;
    }

    printf("  Signature      : 0x%04X (%s)\n", vol->signature, vol->is_hfsx ? "HFSX" : "HFS+");
    printf("  Block Size     : %u bytes\n", vol->block_size);
    printf("  Total Blocks   : %u (%.2f GB)\n", vol->total_blocks,
           (double)((uint64_t)vol->total_blocks * vol->block_size) / (1024.0 * 1024.0 * 1024.0));
    printf("  Free Blocks    : %u\n", vol->free_blocks);
    printf("  Total Files    : %u\n", vol->file_count);
    printf("  Total Folders  : %u\n", vol->folder_count);
    printf("  Catalog File   : size=%llu bytes, totalBlocks=%u\n",
           (unsigned long long)vol->catalog_file.logical_size, vol->catalog_file.total_blocks);

    /* 4. Catalog B-Tree Analysis */
    printf("\n[4/4] Inspecting Catalog B-Tree...\n");
    hfs_catalog_t* cat = hfs_catalog_open(vol);
    if (!cat) {
        printf("  [FAIL] Failed to open Catalog B-Tree!\n");
        hfs_volume_close(vol);
        return -1;
    }

    hfs_btree_t* bt = cat->btree;
    printf("  Node Size      : %u bytes\n", bt->node_size);
    printf("  Tree Depth     : %u\n", bt->header.treeDepth);
    printf("  Root Node      : %u\n", bt->header.rootNode);
    printf("  Leaf Records   : %u\n", bt->header.leafRecords);
    printf("  First Leaf Node: %u\n", bt->header.firstLeafNode);
    printf("  Last Leaf Node : %u\n", bt->header.lastLeafNode);

    printf("\n--- DUMPING FIRST 50 CATALOG RECORDS FROM LEAF NODES ---\n");
    uint32_t leaf = bt->header.firstLeafNode;
    uint8_t* nbuf = bt->node_buffer;
    int dumped = 0;
    int root_children = 0;

    while (leaf != 0 && dumped < 50) {
        if (hfs_btree_read_node(bt, leaf, nbuf) != 0) break;
        const BTNodeDescriptor* desc = (const BTNodeDescriptor*)nbuf;
        uint16_t num_rec = be16_to_cpu(desc->numRecords);

        for (uint16_t i = 0; i < num_rec && dumped < 50; i++) {
            void* k = NULL;
            void* d = NULL;
            size_t dlen = 0;
            if (hfs_btree_get_node_record(bt, nbuf, i, &k, &d, &dlen) != 0) continue;

            const HFSPlusCatalogKey* ckey = (const HFSPlusCatalogKey*)k;
            uint32_t pid = be32_to_cpu(ckey->parentID);
            uint16_t rtype = (dlen >= 2) ? be16_to_cpu(*(const uint16_t*)d) : 0;

            char name[256];
            hfs_unistr_to_utf8(&ckey->nodeName, name, sizeof(name));

            const char* type_str = "Unknown";
            uint32_t cnid = 0;
            uint64_t fsize = 0;

            if (rtype == kHFSPlusFolderRecord) {
                type_str = "DIR ";
                const HFSPlusCatalogFolder* f = (const HFSPlusCatalogFolder*)d;
                cnid = be32_to_cpu(f->folderID);
            } else if (rtype == kHFSPlusFileRecord) {
                type_str = "FILE";
                const HFSPlusCatalogFile* f = (const HFSPlusCatalogFile*)d;
                cnid = be32_to_cpu(f->fileID);
                fsize = be64_to_cpu(f->dataFork.logicalSize);
            } else if (rtype == kHFSPlusFolderThreadRecord) {
                type_str = "FTHR";
            } else if (rtype == kHFSPlusFileThreadRecord) {
                type_str = "LTHR";
            }

            if (pid == kHFSRootFolderID && (rtype == kHFSPlusFolderRecord || rtype == kHFSPlusFileRecord)) {
                root_children++;
            }

            printf("  #%02d [Leaf %u, Rec %u]: parentID=%-5u CNID=%-5u Type=%s Size=%-8llu Name=\"%s\"\n",
                   dumped + 1, leaf, i, pid, cnid, type_str, (unsigned long long)fsize, name);
            dumped++;
        }

        leaf = be32_to_cpu(desc->fLink);
    }

    printf("\nTotal files/folders directly in Root Folder (CNID=2): %d\n", root_children);
    printf("=======================================================\n");

    hfs_catalog_close(cat);
    hfs_volume_close(vol);
    return 0;
}
