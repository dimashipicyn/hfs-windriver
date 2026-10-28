#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include "hfsplus/volume.h"
#include "hfsplus/hfsplus_endian.h"
#include "hfsplus/mem.h"

static void test_endian_and_time(void) {
    printf("[TEST] Testing endian macros and time conversions...\n");

    /* Endian swaps */
    uint16_t val16 = 0x1234;
    assert(be16_to_cpu(cpu_to_be16(val16)) == val16);

    uint32_t val32 = 0x12345678;
    assert(be32_to_cpu(cpu_to_be32(val32)) == val32);

    uint64_t val64 = 0x1122334455667788ULL;
    assert(be64_to_cpu(cpu_to_be64(val64)) == val64);

    /* Time conversions */
    /* Jan 1 1970 00:00:00 UTC */
    uint32_t hfs_1970 = 2082844800U;
    time_t unix_time = hfs_to_unix_time(hfs_1970);
    assert(unix_time == 0);
    assert(unix_to_hfs_time(0) == hfs_1970);

    /* Windows FILETIME for Jan 1 1970 is 116444736000000000 */
    uint64_t win_ft = hfs_to_windows_filetime(hfs_1970);
    assert(win_ft == 116444736000000000ULL);

    printf("  -> Endian and time conversions OK!\n");
}

static void test_synthetic_volume_mount(void) {
    printf("[TEST] Testing synthetic volume mount and backup header...\n");

    const char* tmp_file = "test_hfs_synth.bin";
    const size_t total_size = 1024 * 1024; /* 1MB */

    uint8_t* buffer = (uint8_t*)calloc(1, total_size);
    assert(buffer != NULL);

    /* Construct primary HFS+ Volume Header at offset 1024 */
    HFSPlusVolumeHeader* vh = (HFSPlusVolumeHeader*)(buffer + 1024);
    vh->signature = cpu_to_be16(kHFSPlusSigWord);
    vh->version = cpu_to_be16(kHFSPlusVersion);
    vh->blockSize = cpu_to_be32(4096);
    vh->totalBlocks = cpu_to_be32(256);
    vh->freeBlocks = cpu_to_be32(200);
    vh->fileCount = cpu_to_be32(10);
    vh->folderCount = cpu_to_be32(2);

    /* Write to temporary file */
    FILE* fp = fopen(tmp_file, "wb");
    assert(fp != NULL);
    assert(fwrite(buffer, 1, total_size, fp) == total_size);
    fclose(fp);

    /* Open block device and mount */
    hfs_block_device_t* dev = hfs_block_device_open_file(tmp_file);
    assert(dev != NULL);
    assert(dev->total_size == total_size);

    hfs_volume_t* vol = hfs_volume_open(dev);
    assert(vol != NULL);
    assert(vol->signature == kHFSPlusSigWord);
    assert(vol->version == kHFSPlusVersion);
    assert(vol->block_size == 4096);
    assert(vol->total_blocks == 256);
    assert(vol->free_blocks == 200);
    assert(vol->file_count == 10);
    assert(vol->folder_count == 2);
    assert(!vol->is_hfsx);
    assert(!vol->mounted_from_backup_header);

    hfs_volume_close(vol);
    hfs_block_device_close(dev);

    /* Test backup header: Corrupt primary header, put valid header at total_size - 1024 */
    vh->signature = 0; /* corrupt primary */
    HFSPlusVolumeHeader* backup_vh = (HFSPlusVolumeHeader*)(buffer + total_size - 1024);
    backup_vh->signature = cpu_to_be16(kHFSPlusSigWord);
    backup_vh->version = cpu_to_be16(kHFSPlusVersion);
    backup_vh->blockSize = cpu_to_be32(4096);
    backup_vh->totalBlocks = cpu_to_be32(256);
    backup_vh->freeBlocks = cpu_to_be32(150);

    fp = fopen(tmp_file, "wb");
    assert(fp != NULL);
    assert(fwrite(buffer, 1, total_size, fp) == total_size);
    fclose(fp);
    free(buffer);

    dev = hfs_block_device_open_file(tmp_file);
    assert(dev != NULL);
    vol = hfs_volume_open(dev);
    assert(vol != NULL);
    assert(vol->mounted_from_backup_header == true);
    assert(vol->free_blocks == 150);

    hfs_volume_close(vol);
    hfs_block_device_close(dev);
    remove(tmp_file);

    printf("  -> Synthetic volume and backup header OK!\n");
}

int main(int argc, char** argv) {
    printf("=== Running HFS+ Volume Tests ===\n");
    test_endian_and_time();
    test_synthetic_volume_mount();

    /* If a real disk image argument is provided, test it */
    if (argc > 1) {
        printf("[TEST] Testing real disk image: %s\n", argv[1]);
        hfs_block_device_t* dev = hfs_block_device_open_file(argv[1]);
        if (!dev) {
            fprintf(stderr, "Failed to open device: %s\n", argv[1]);
            return 1;
        }
        hfs_volume_t* vol = hfs_volume_open(dev);
        if (!vol) {
            fprintf(stderr, "Failed to mount HFS+ volume from %s\n", argv[1]);
            hfs_block_device_close(dev);
            return 1;
        }

        printf("  Signature: 0x%04X (%s)\n", vol->signature, vol->is_hfsx ? "HFSX" : "HFS+");
        printf("  Block Size: %u bytes\n", vol->block_size);
        printf("  Total Blocks: %u (%llu MB)\n", vol->total_blocks,
               (unsigned long long)(vol->total_blocks * vol->block_size) / (1024 * 1024));
        printf("  Free Blocks: %u\n", vol->free_blocks);
        printf("  Files: %u, Folders: %u\n", vol->file_count, vol->folder_count);
        printf("  Journaled: %s\n", vol->is_journaled ? "YES" : "NO");
        printf("  Mounted from: %s header\n", vol->mounted_from_backup_header ? "BACKUP" : "PRIMARY");

        hfs_volume_close(vol);
        hfs_block_device_close(dev);
        printf("  -> Real disk image test PASSED!\n");
    }

    printf("=== All Volume Tests PASSED ===\n");
    return 0;
}
