#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include "hfsplus/volume.h"
#include "hfsplus/catalog.h"
#include "hfsplus/fork.h"
#include "hfsplus/hfsplus_endian.h"
#include "hfsplus/mem.h"

typedef struct {
    int count;
    char found_names[64][256];
} dir_iter_ctx_t;

static int on_dir_entry(const hfs_dirent_t* entry, void* user_data) {
    dir_iter_ctx_t* ctx = (dir_iter_ctx_t*)user_data;
    if (ctx->count < 64) {
        strncpy(ctx->found_names[ctx->count], entry->name, 255);
        ctx->found_names[ctx->count][255] = '\0';
        ctx->count++;
    }
    printf("    [ENTRY] %s (CNID=%u, is_dir=%d, size=%llu)\n",
           entry->name, entry->cnid, entry->is_directory, (unsigned long long)entry->size);
    return 0;
}

static void test_real_catalog_image(const char* img_path) {
    printf("[TEST] Testing Catalog on real disk image: %s\n", img_path);

    hfs_block_device_t* dev = hfs_block_device_open_file(img_path);
    assert(dev != NULL);

    hfs_volume_t* vol = hfs_volume_open(dev);
    assert(vol != NULL);

    hfs_catalog_t* cat = hfs_catalog_open(vol);
    assert(cat != NULL);

    /* 1. Lookup Root */
    hfs_dirent_t root_ent;
    int res = hfs_catalog_lookup_path(cat, "/", &root_ent);
    assert(res == 0);
    assert(root_ent.cnid == kHFSRootFolderID);
    assert(root_ent.is_directory);
    printf("  -> Root folder found: CNID=%u\n", root_ent.cnid);

    /* 2. Enumerate Root Folder */
    printf("  Enumerating Root directory:\n");
    dir_iter_ctx_t root_ctx = {0};
    res = hfs_catalog_iterate_dir(cat, kHFSRootFolderID, on_dir_entry, &root_ctx);
    assert(res == 0);
    assert(root_ctx.count > 0);
    printf("  -> Root contains %d items\n", root_ctx.count);

    /* 3. Lookup "/test_folder/hello.txt" */
    hfs_dirent_t file_ent;
    res = hfs_catalog_lookup_path(cat, "/test_folder/hello.txt", &file_ent);
    if (res == 0) {
        printf("  -> Found /test_folder/hello.txt: CNID=%u, size=%llu\n",
               file_ent.cnid, (unsigned long long)file_ent.size);
        assert(!file_ent.is_directory);
        assert(file_ent.size > 0);

        /* Read content of file */
        char* file_content = (char*)hfs_zalloc(file_ent.size + 1);
        assert(file_content != NULL);

        int read_res = hfs_fork_read(vol, &file_ent.data_fork, 0, file_content, file_ent.size);
        assert(read_res == 0);
        printf("  -> Content read: \"%s\"\n", file_content);
        assert(strstr(file_content, "Hello, Windows 11") != NULL);
        hfs_free(file_content);
    }

    /* 4. Lookup Cyrillic UTF-8 filename: "/test_folder/nested/russian_привет.txt" */
    hfs_dirent_t rus_ent;
    res = hfs_catalog_lookup_path(cat, "/test_folder/nested/russian_привет.txt", &rus_ent);
    if (res == 0) {
        printf("  -> Found /test_folder/nested/russian_привет.txt: CNID=%u, size=%llu\n",
               rus_ent.cnid, (unsigned long long)rus_ent.size);
        assert(!rus_ent.is_directory);
        char* rus_content = (char*)hfs_zalloc(rus_ent.size + 1);
        assert(rus_content != NULL);
        int read_res = hfs_fork_read(vol, &rus_ent.data_fork, 0, rus_content, rus_ent.size);
        assert(read_res == 0);
        printf("  -> Cyrillic content read: \"%s\"\n", rus_content);
        assert(strstr(rus_content, "Привет") != NULL);
        hfs_free(rus_content);
    }

    hfs_catalog_close(cat);
    hfs_volume_close(vol);
    hfs_block_device_close(dev);

    printf("  -> Real catalog test PASSED!\n");
}

int main(int argc, char** argv) {
    if (argc > 1) {
        test_real_catalog_image(argv[1]);
        return 0;
    }

#if defined(__APPLE__)
    printf("=== Creating dynamic HFS+ fixture on macOS ===\n");
    const char* dmg_path = "/tmp/test_hfs_fixture_auto.dmg";
    const char* mount_point = "/tmp/test_hfs_mnt_auto";

    char cmd[512];
    snprintf(cmd, sizeof(cmd), "rm -f %s && hdiutil create -size 16m -fs HFS+ -volname AutoTest -layout NONE %s > /dev/null",
             dmg_path, dmg_path);
    if (system(cmd) != 0) {
        fprintf(stderr, "Failed to create test dmg with hdiutil\n");
        return 1;
    }

    snprintf(cmd, sizeof(cmd), "mkdir -p %s && hdiutil attach %s -mountpoint %s > /dev/null",
             mount_point, dmg_path, mount_point);
    if (system(cmd) == 0) {
        snprintf(cmd, sizeof(cmd),
                 "mkdir -p %s/test_folder/nested && "
                 "echo 'Hello, Windows 11 HFS+ driver!' > %s/test_folder/hello.txt && "
                 "echo 'Привет из HFS+!' > %s/test_folder/nested/russian_привет.txt && sync",
                 mount_point, mount_point, mount_point);
        (void)system(cmd);

        snprintf(cmd, sizeof(cmd), "hdiutil detach %s > /dev/null && rmdir %s",
                 mount_point, mount_point);
        (void)system(cmd);
    }

    test_real_catalog_image(dmg_path);

    snprintf(cmd, sizeof(cmd), "rm -f %s", dmg_path);
    (void)system(cmd);

    printf("=== Dynamic Fixture Test PASSED ===\n");
    return 0;
#else
    printf("Skipping dynamic image test on non-Apple platform. Pass image path explicitly.\n");
    return 0;
#endif
}
