#include "hfsplus_winfsp.h"
#include "hfsplus/fork.h"
#include "hfsplus/hfsplus_endian.h"
#include "hfsplus/mem.h"

#ifdef _WIN32
#include <stdio.h>

static void convert_wchar_to_utf8(PWSTR wstr, char* utf8, size_t max_bytes) {
    if (!wstr || !utf8 || max_bytes == 0) return;
    WideCharToMultiByte(CP_UTF8, 0, wstr, -1, utf8, (int)max_bytes, NULL, NULL);
    utf8[max_bytes - 1] = '\0';
}

static void fill_file_info(const hfs_volume_t* vol, const hfs_dirent_t* ent, FSP_FSCTL_FILE_INFO* info) {
    memset(info, 0, sizeof(*info));

    if (ent->is_directory) {
        info->FileAttributes = FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_READONLY;
    } else {
        info->FileAttributes = FILE_ATTRIBUTE_ARCHIVE | FILE_ATTRIBUTE_READONLY;
        info->FileSize = ent->size;
        uint32_t bs = vol->block_size ? vol->block_size : 4096;
        info->AllocationSize = ((ent->size + bs - 1) / bs) * bs;
    }

    info->CreationTime = hfs_to_windows_filetime(ent->create_date);
    info->LastWriteTime = hfs_to_windows_filetime(ent->modify_date);
    info->ChangeTime = info->LastWriteTime;
    info->LastAccessTime = hfs_to_windows_filetime(ent->access_date);
    info->IndexNumber = ent->cnid;
}

static NTSTATUS GetVolumeInfo(FSP_FILE_SYSTEM* FileSystem, FSP_FSCTL_VOLUME_INFO* VolumeInfo) {
    hfs_winfsp_context_t* ctx = (hfs_winfsp_context_t*)FileSystem->UserContext;
    const hfs_volume_t* vol = ctx->vol;

    VolumeInfo->TotalSize = (UINT64)vol->total_blocks * (UINT64)vol->block_size;
    VolumeInfo->FreeSize = (UINT64)vol->free_blocks * (UINT64)vol->block_size;

    const wchar_t* label = vol->is_hfsx ? L"Mac HFSX" : L"Mac HFS+";
    wcsncpy_s(VolumeInfo->VolumeLabel, sizeof(VolumeInfo->VolumeLabel)/sizeof(wchar_t), label, _TRUNCATE);

    return STATUS_SUCCESS;
}

static NTSTATUS Open(FSP_FILE_SYSTEM* FileSystem, PWSTR FileName, UINT32 CreateOptions,
                     UINT32 GrantedAccess, PVOID* PFileContext, FSP_FSCTL_FILE_INFO* FileInfo) {
    (void)CreateOptions;
    (void)GrantedAccess;

    hfs_winfsp_context_t* ctx = (hfs_winfsp_context_t*)FileSystem->UserContext;

    char utf8_path[1024];
    convert_wchar_to_utf8(FileName, utf8_path, sizeof(utf8_path));

    hfs_dirent_t ent;
    if (hfs_catalog_lookup_path(ctx->cat, utf8_path, &ent) != 0) {
        return STATUS_OBJECT_NAME_NOT_FOUND;
    }

    hfs_winfsp_file_context_t* fctx = (hfs_winfsp_file_context_t*)hfs_zalloc(sizeof(hfs_winfsp_file_context_t));
    if (!fctx) {
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    fctx->dirent = ent;
    fill_file_info(ctx->vol, &fctx->dirent, FileInfo);
    *PFileContext = fctx;

    return STATUS_SUCCESS;
}

static VOID Close(FSP_FILE_SYSTEM* FileSystem, PVOID FileContext) {
    (void)FileSystem;
    hfs_free(FileContext);
}

static NTSTATUS Read(FSP_FILE_SYSTEM* FileSystem, PVOID FileContext, PVOID Buffer,
                     UINT64 Offset, UINT32 Length, PUINT32 PBytesTransferred) {
    hfs_winfsp_context_t* ctx = (hfs_winfsp_context_t*)FileSystem->UserContext;
    hfs_winfsp_file_context_t* fctx = (hfs_winfsp_file_context_t*)FileContext;

    if (fctx->dirent.is_directory) {
        return STATUS_ACCESS_DENIED;
    }

    if (Offset >= fctx->dirent.size) {
        *PBytesTransferred = 0;
        return STATUS_END_OF_FILE;
    }

    if (Offset + Length > fctx->dirent.size) {
        Length = (UINT32)(fctx->dirent.size - Offset);
    }

    if (hfs_fork_read(ctx->vol, &fctx->dirent.data_fork, Offset, Buffer, Length) != 0) {
        return STATUS_UNSUCCESSFUL;
    }

    *PBytesTransferred = Length;
    return STATUS_SUCCESS;
}

static NTSTATUS GetFileInfo(FSP_FILE_SYSTEM* FileSystem, PVOID FileContext, FSP_FSCTL_FILE_INFO* FileInfo) {
    hfs_winfsp_context_t* ctx = (hfs_winfsp_context_t*)FileSystem->UserContext;
    hfs_winfsp_file_context_t* fctx = (hfs_winfsp_file_context_t*)FileContext;

    fill_file_info(ctx->vol, &fctx->dirent, FileInfo);
    return STATUS_SUCCESS;
}

typedef struct {
    FSP_FILE_SYSTEM* FileSystem;
    PVOID Buffer;
    ULONG Length;
    PULONG PBytesTransferred;
} dir_fill_ctx_t;

static int on_winfsp_dir_entry(const hfs_dirent_t* entry, void* user_data) {
    dir_fill_ctx_t* dctx = (dir_fill_ctx_t*)user_data;

    wchar_t wname[256];
    MultiByteToWideChar(CP_UTF8, 0, entry->name, -1, wname, 256);

    UINT8 dir_info_buf[sizeof(FSP_FSCTL_DIR_INFO) + sizeof(wname)];
    FSP_FSCTL_DIR_INFO* dir_info = (FSP_FSCTL_DIR_INFO*)dir_info_buf;
    memset(dir_info, 0, sizeof(*dir_info));

    hfs_winfsp_context_t* ctx = (hfs_winfsp_context_t*)dctx->FileSystem->UserContext;
    fill_file_info(ctx->vol, entry, &dir_info->FileInfo);

    dir_info->Size = (UINT16)(sizeof(FSP_FSCTL_DIR_INFO) + wcslen(wname) * sizeof(wchar_t));
    wcscpy_s((wchar_t*)dir_info->FileNameBuf, sizeof(wname)/sizeof(wchar_t), wname);

    if (!FspFileSystemAddDirInfo(dir_info, dctx->Buffer, dctx->Length, dctx->PBytesTransferred)) {
        return 1; /* Buffer full, stop iteration */
    }

    return 0;
}

static NTSTATUS ReadDirectory(FSP_FILE_SYSTEM* FileSystem, PVOID FileContext,
                             PWSTR Pattern, PWSTR Marker, PVOID Buffer, UINT32 Length,
                             PUINT32 PBytesTransferred) {
    (void)Pattern;
    (void)Marker;

    hfs_winfsp_context_t* ctx = (hfs_winfsp_context_t*)FileSystem->UserContext;
    hfs_winfsp_file_context_t* fctx = (hfs_winfsp_file_context_t*)FileContext;

    if (!fctx->dirent.is_directory) {
        return STATUS_NOT_A_DIRECTORY;
    }

    dir_fill_ctx_t dctx = {
        .FileSystem = FileSystem,
        .Buffer = Buffer,
        .Length = (ULONG)Length,
        .PBytesTransferred = (PULONG)PBytesTransferred,
    };

    hfs_catalog_iterate_dir(ctx->cat, fctx->dirent.cnid, on_winfsp_dir_entry, &dctx);

    /* Signal EOF marker for directory listing */
    FspFileSystemAddDirInfo(NULL, Buffer, (ULONG)Length, (PULONG)PBytesTransferred);

    return STATUS_SUCCESS;
}

static const FSP_FILE_SYSTEM_INTERFACE g_hfs_interface = {
    .GetVolumeInfo = GetVolumeInfo,
    .Open = Open,
    .Close = Close,
    .Read = Read,
    .GetFileInfo = GetFileInfo,
    .ReadDirectory = ReadDirectory,
};

int hfs_winfsp_mount(const wchar_t* device_or_image_path, const wchar_t* mount_point, bool debug_mode) {
    (void)debug_mode;

    hfs_block_device_t* dev = hfs_block_device_open_win32(device_or_image_path);
    if (!dev) {
        fwprintf(stderr, L"Failed to open device: %ls\n", device_or_image_path);
        return -1;
    }

    hfs_volume_t* vol = hfs_volume_open(dev);
    if (!vol) {
        fwprintf(stderr, L"Device does not contain a valid HFS+ volume: %ls\n", device_or_image_path);
        hfs_block_device_close(dev);
        return -1;
    }

    hfs_catalog_t* cat = hfs_catalog_open(vol);
    if (!cat) {
        fwprintf(stderr, L"Failed to open HFS+ catalog.\n");
        hfs_volume_close(vol);
        hfs_block_device_close(dev);
        return -1;
    }

    hfs_winfsp_context_t* ctx = (hfs_winfsp_context_t*)hfs_zalloc(sizeof(hfs_winfsp_context_t));
    if (!ctx) {
        hfs_catalog_close(cat);
        hfs_volume_close(vol);
        hfs_block_device_close(dev);
        return -1;
    }

    ctx->dev = dev;
    ctx->vol = vol;
    ctx->cat = cat;

    FSP_FSCTL_VOLUME_PARAMS params;
    memset(&params, 0, sizeof(params));
    params.SectorSize = 512;
    params.SectorsPerAllocationUnit = (UINT16)(vol->block_size / 512);
    params.MaxComponentLength = 255;
    params.VolumeCreationTime = hfs_to_windows_filetime(vol->create_date);
    params.VolumeSerialNumber = (UINT32)vol->create_date;
    params.ReadOnlyVolume = 1;
    params.CasePreservedNames = 1;
    params.UnicodeOnDisk = 1;

    NTSTATUS status = FspFileSystemCreate((PWSTR)FSP_FSCTL_DISK_DEVICE_NAME, &params, &g_hfs_interface, &ctx->fsp);
    if (!NT_SUCCESS(status)) {
        fwprintf(stderr, L"FspFileSystemCreate failed with status 0x%08X\n", (unsigned int)status);
        hfs_free(ctx);
        hfs_catalog_close(cat);
        hfs_volume_close(vol);
        hfs_block_device_close(dev);
        return -1;
    }

    ctx->fsp->UserContext = ctx;

    status = FspFileSystemSetMountPoint(ctx->fsp, (PWSTR)mount_point);
    if (!NT_SUCCESS(status)) {
        fwprintf(stderr, L"FspFileSystemSetMountPoint failed with status 0x%08X\n", (unsigned int)status);
        FspFileSystemDelete(ctx->fsp);
        hfs_free(ctx);
        hfs_catalog_close(cat);
        hfs_volume_close(vol);
        hfs_block_device_close(dev);
        return -1;
    }

    wprintf(L"Mounted HFS+ volume [%ls] at [%ls] (Read-Only)\n", device_or_image_path, mount_point);
    status = FspFileSystemStartDispatcher(ctx->fsp, 0);

    return NT_SUCCESS(status) ? 0 : -1;
}

#endif /* _WIN32 */
