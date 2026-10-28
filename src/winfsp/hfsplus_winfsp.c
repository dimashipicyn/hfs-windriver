#include "hfsplus_winfsp.h"
#include "hfsplus/fork.h"
#include "hfsplus/hfsplus_endian.h"
#include "hfsplus/mem.h"

#ifdef _WIN32
#include <stdio.h>
#include <sddl.h>
#include <shlobj.h>

static PSECURITY_DESCRIPTOR g_default_sd = NULL;
static ULONG g_default_sd_size = 0;

static void init_security_descriptor(void) {
    if (!g_default_sd) {
        ConvertStringSecurityDescriptorToSecurityDescriptorW(
            L"O:BAG:BAD:P(A;;FA;;;SY)(A;;FA;;;BA)(A;;FA;;;WD)",
            SDDL_REVISION_1,
            &g_default_sd,
            &g_default_sd_size
        );
    }
}

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
    EnterCriticalSection(&ctx->lock);

    const hfs_volume_t* vol = ctx->vol;

    VolumeInfo->TotalSize = (UINT64)vol->total_blocks * (UINT64)vol->block_size;
    VolumeInfo->FreeSize = (UINT64)vol->free_blocks * (UINT64)vol->block_size;

    const wchar_t* label = vol->is_hfsx ? L"Mac HFSX" : L"Mac HFS+";
    wcsncpy_s(VolumeInfo->VolumeLabel, sizeof(VolumeInfo->VolumeLabel)/sizeof(wchar_t), label, _TRUNCATE);
    VolumeInfo->VolumeLabelLength = (UINT16)(wcslen(VolumeInfo->VolumeLabel) * sizeof(wchar_t));

    LeaveCriticalSection(&ctx->lock);
    return STATUS_SUCCESS;
}

static NTSTATUS GetSecurityByName(FSP_FILE_SYSTEM* FileSystem, PWSTR FileName,
                                 PUINT32 PFileAttributes, PSECURITY_DESCRIPTOR SecurityDescriptor,
                                 SIZE_T* PSecurityDescriptorSize) {
    hfs_winfsp_context_t* ctx = (hfs_winfsp_context_t*)FileSystem->UserContext;

    init_security_descriptor();

    char utf8_path[1024];
    convert_wchar_to_utf8(FileName, utf8_path, sizeof(utf8_path));

    EnterCriticalSection(&ctx->lock);

    hfs_dirent_t ent;
    if (hfs_catalog_lookup_path(ctx->cat, utf8_path, &ent) != 0) {
        LeaveCriticalSection(&ctx->lock);
        return STATUS_OBJECT_NAME_NOT_FOUND;
    }

    if (PFileAttributes) {
        if (ent.is_directory) {
            *PFileAttributes = FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_READONLY;
        } else {
            *PFileAttributes = FILE_ATTRIBUTE_ARCHIVE | FILE_ATTRIBUTE_READONLY;
        }
    }

    if (PSecurityDescriptorSize) {
        if (SecurityDescriptor && *PSecurityDescriptorSize >= g_default_sd_size) {
            memcpy(SecurityDescriptor, g_default_sd, g_default_sd_size);
        } else if (SecurityDescriptor) {
            *PSecurityDescriptorSize = g_default_sd_size;
            LeaveCriticalSection(&ctx->lock);
            return STATUS_BUFFER_TOO_SMALL;
        }
        *PSecurityDescriptorSize = g_default_sd_size;
    }

    LeaveCriticalSection(&ctx->lock);
    return STATUS_SUCCESS;
}

static NTSTATUS GetSecurity(FSP_FILE_SYSTEM* FileSystem, PVOID FileContext,
                           PSECURITY_DESCRIPTOR SecurityDescriptor, SIZE_T* PSecurityDescriptorSize) {
    (void)FileSystem;
    (void)FileContext;

    init_security_descriptor();

    if (PSecurityDescriptorSize) {
        if (SecurityDescriptor && *PSecurityDescriptorSize >= g_default_sd_size) {
            memcpy(SecurityDescriptor, g_default_sd, g_default_sd_size);
        } else if (SecurityDescriptor) {
            *PSecurityDescriptorSize = g_default_sd_size;
            return STATUS_BUFFER_TOO_SMALL;
        }
        *PSecurityDescriptorSize = g_default_sd_size;
    }
    return STATUS_SUCCESS;
}

static NTSTATUS Open(FSP_FILE_SYSTEM* FileSystem, PWSTR FileName, UINT32 CreateOptions,
                     UINT32 GrantedAccess, PVOID* PFileContext, FSP_FSCTL_FILE_INFO* FileInfo) {
    (void)CreateOptions;
    (void)GrantedAccess;

    hfs_winfsp_context_t* ctx = (hfs_winfsp_context_t*)FileSystem->UserContext;

    char utf8_path[1024];
    convert_wchar_to_utf8(FileName, utf8_path, sizeof(utf8_path));

    EnterCriticalSection(&ctx->lock);

    hfs_dirent_t ent;
    if (hfs_catalog_lookup_path(ctx->cat, utf8_path, &ent) != 0) {
        LeaveCriticalSection(&ctx->lock);
        return STATUS_OBJECT_NAME_NOT_FOUND;
    }

    hfs_winfsp_file_context_t* fctx = (hfs_winfsp_file_context_t*)hfs_zalloc(sizeof(hfs_winfsp_file_context_t));
    if (!fctx) {
        LeaveCriticalSection(&ctx->lock);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    fctx->dirent = ent;
    fill_file_info(ctx->vol, &fctx->dirent, FileInfo);
    *PFileContext = fctx;

    printf("[HFS+] Open: %s (CNID=%u, %s)\n", utf8_path, ent.cnid, ent.is_directory ? "DIR" : "FILE");
    LeaveCriticalSection(&ctx->lock);
    return STATUS_SUCCESS;
}

static VOID Close(FSP_FILE_SYSTEM* FileSystem, PVOID FileContext) {
    (void)FileSystem;
    hfs_winfsp_file_context_t* fctx = (hfs_winfsp_file_context_t*)FileContext;
    if (fctx) {
        if (fctx->dir_buffer) {
            FspFileSystemDeleteDirectoryBuffer(&fctx->dir_buffer);
        }
        hfs_free(fctx);
    }
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

    EnterCriticalSection(&ctx->lock);
    int res = hfs_fork_read(ctx->vol, &fctx->dirent.data_fork, Offset, Buffer, Length);
    LeaveCriticalSection(&ctx->lock);

    if (res != 0) {
        return STATUS_UNSUCCESSFUL;
    }

    *PBytesTransferred = Length;
    return STATUS_SUCCESS;
}

static NTSTATUS GetFileInfo(FSP_FILE_SYSTEM* FileSystem, PVOID FileContext, FSP_FSCTL_FILE_INFO* FileInfo) {
    hfs_winfsp_context_t* ctx = (hfs_winfsp_context_t*)FileSystem->UserContext;
    hfs_winfsp_file_context_t* fctx = (hfs_winfsp_file_context_t*)FileContext;

    EnterCriticalSection(&ctx->lock);
    fill_file_info(ctx->vol, &fctx->dirent, FileInfo);
    LeaveCriticalSection(&ctx->lock);
    return STATUS_SUCCESS;
}

static NTSTATUS GetDirInfoByName(FSP_FILE_SYSTEM* FileSystem, PVOID FileContext,
                                 PWSTR FileName, FSP_FSCTL_DIR_INFO* DirInfo) {
    hfs_winfsp_context_t* ctx = (hfs_winfsp_context_t*)FileSystem->UserContext;
    hfs_winfsp_file_context_t* fctx = (hfs_winfsp_file_context_t*)FileContext;

    if (!fctx->dirent.is_directory) {
        return STATUS_NOT_A_DIRECTORY;
    }

    if (!FileName || !FileName[0]) {
        return STATUS_OBJECT_NAME_NOT_FOUND;
    }

    EnterCriticalSection(&ctx->lock);

    /* Handle "." */
    if (wcscmp(FileName, L".") == 0) {
        fill_file_info(ctx->vol, &fctx->dirent, &DirInfo->FileInfo);
        DirInfo->Size = (UINT16)(sizeof(FSP_FSCTL_DIR_INFO) + 1 * sizeof(wchar_t));
        DirInfo->FileNameBuf[0] = L'.';
        LeaveCriticalSection(&ctx->lock);
        return STATUS_SUCCESS;
    }

    char utf8_name[256];
    convert_wchar_to_utf8(FileName, utf8_name, sizeof(utf8_name));

    hfs_dirent_t ent;
    if (hfs_catalog_lookup(ctx->cat, fctx->dirent.cnid, utf8_name, &ent) != 0) {
        LeaveCriticalSection(&ctx->lock);
        return STATUS_OBJECT_NAME_NOT_FOUND;
    }

    fill_file_info(ctx->vol, &ent, &DirInfo->FileInfo);
    size_t name_len = wcslen(FileName);
    DirInfo->Size = (UINT16)(sizeof(FSP_FSCTL_DIR_INFO) + name_len * sizeof(wchar_t));
    memcpy(DirInfo->FileNameBuf, FileName, name_len * sizeof(wchar_t));

    LeaveCriticalSection(&ctx->lock);
    return STATUS_SUCCESS;
}

typedef struct {
    PVOID* pdir_buffer;
    const hfs_volume_t* vol;
    int count;
    NTSTATUS result;
} dir_fill_ctx_t;

static int on_winfsp_dir_entry(const hfs_dirent_t* entry, void* user_data) {
    dir_fill_ctx_t* dctx = (dir_fill_ctx_t*)user_data;

    if (!entry->name[0]) {
        return 0; /* Skip entries without name */
    }

    wchar_t wname[256];
    MultiByteToWideChar(CP_UTF8, 0, entry->name, -1, wname, 256);
    size_t name_len = wcslen(wname);
    if (name_len == 0) return 0;

    /* Sanitize illegal Windows filename characters and control characters */
    for (size_t i = 0; i < name_len; i++) {
        if (wname[i] < 32 || wname[i] == L':' || wname[i] == L'/' || wname[i] == L'\\' ||
            wname[i] == L'*' || wname[i] == L'?' || wname[i] == L'\"' ||
            wname[i] == L'<' || wname[i] == L'>' || wname[i] == L'|') {
            wname[i] = L'_';
        }
    }

    UINT8 dir_info_buf[sizeof(FSP_FSCTL_DIR_INFO) + sizeof(wname)];
    FSP_FSCTL_DIR_INFO* dir_info = (FSP_FSCTL_DIR_INFO*)dir_info_buf;
    memset(dir_info, 0, sizeof(*dir_info));

    fill_file_info(dctx->vol, entry, &dir_info->FileInfo);

    dir_info->Size = (UINT16)(sizeof(FSP_FSCTL_DIR_INFO) + name_len * sizeof(wchar_t));
    memcpy(dir_info->FileNameBuf, wname, name_len * sizeof(wchar_t));

    NTSTATUS result = STATUS_SUCCESS;
    if (!FspFileSystemFillDirectoryBuffer(dctx->pdir_buffer, dir_info, &result)) {
        dctx->result = result;
        return 1; /* Buffer full or error */
    }

    printf("  [+] %s (%s, size=%llu)\n", entry->name, entry->is_directory ? "DIR" : "FILE", (unsigned long long)entry->size);
    dctx->count++;
    return 0;
}

static NTSTATUS ReadDirectory(FSP_FILE_SYSTEM* FileSystem, PVOID FileContext,
                             PWSTR Pattern, PWSTR Marker, PVOID Buffer, UINT32 Length,
                             PUINT32 PBytesTransferred) {
    (void)Pattern;

    hfs_winfsp_context_t* ctx = (hfs_winfsp_context_t*)FileSystem->UserContext;
    hfs_winfsp_file_context_t* fctx = (hfs_winfsp_file_context_t*)FileContext;

    if (!fctx->dirent.is_directory) {
        return STATUS_NOT_A_DIRECTORY;
    }

    EnterCriticalSection(&ctx->lock);

    NTSTATUS dir_result = STATUS_SUCCESS;
    if (FspFileSystemAcquireDirectoryBuffer(&fctx->dir_buffer, 0 == Marker, &dir_result)) {
        /* ONLY add "." and ".." if this is NOT the root folder! */
        if (fctx->dirent.cnid != kHFSRootFolderID) {
            /* Add "." */
            UINT8 dot_buf[sizeof(FSP_FSCTL_DIR_INFO) + 2 * sizeof(wchar_t)];
            FSP_FSCTL_DIR_INFO* dot_info = (FSP_FSCTL_DIR_INFO*)dot_buf;
            memset(dot_info, 0, sizeof(*dot_info));
            fill_file_info(ctx->vol, &fctx->dirent, &dot_info->FileInfo);
            dot_info->Size = (UINT16)(sizeof(FSP_FSCTL_DIR_INFO) + 1 * sizeof(wchar_t));
            dot_info->FileNameBuf[0] = L'.';
            FspFileSystemFillDirectoryBuffer(&fctx->dir_buffer, dot_info, &dir_result);

            /* Add ".." */
            UINT8 dotdot_buf[sizeof(FSP_FSCTL_DIR_INFO) + 3 * sizeof(wchar_t)];
            FSP_FSCTL_DIR_INFO* dotdot_info = (FSP_FSCTL_DIR_INFO*)dotdot_buf;
            memset(dotdot_info, 0, sizeof(*dotdot_info));
            fill_file_info(ctx->vol, &fctx->dirent, &dotdot_info->FileInfo);
            dotdot_info->Size = (UINT16)(sizeof(FSP_FSCTL_DIR_INFO) + 2 * sizeof(wchar_t));
            dotdot_info->FileNameBuf[0] = L'.';
            dotdot_info->FileNameBuf[1] = L'.';
            FspFileSystemFillDirectoryBuffer(&fctx->dir_buffer, dotdot_info, &dir_result);
        }

        /* Enumerate directory entries from HFS+ catalog */
        dir_fill_ctx_t dctx = {
            .pdir_buffer = &fctx->dir_buffer,
            .vol = ctx->vol,
            .count = 0,
            .result = STATUS_SUCCESS,
        };
        hfs_catalog_iterate_dir(ctx->cat, fctx->dirent.cnid, on_winfsp_dir_entry, &dctx);

        printf("[HFS+] ReadDirectory: CNID=%u, entries listed: %d\n", fctx->dirent.cnid, dctx.count);
        FspFileSystemReleaseDirectoryBuffer(&fctx->dir_buffer);
    }

    if (!NT_SUCCESS(dir_result)) {
        LeaveCriticalSection(&ctx->lock);
        return dir_result;
    }

    FspFileSystemReadDirectoryBuffer(&fctx->dir_buffer, Marker, Buffer, (ULONG)Length, (PULONG)PBytesTransferred);
    LeaveCriticalSection(&ctx->lock);
    return STATUS_SUCCESS;
}

static const FSP_FILE_SYSTEM_INTERFACE g_hfs_interface = {
    .GetVolumeInfo = GetVolumeInfo,
    .GetSecurityByName = GetSecurityByName,
    .GetSecurity = GetSecurity,
    .Open = Open,
    .Close = Close,
    .Read = Read,
    .GetFileInfo = GetFileInfo,
    .ReadDirectory = ReadDirectory,
    .GetDirInfoByName = GetDirInfoByName,
};

static HANDLE g_stop_event = NULL;

static BOOL WINAPI console_ctrl_handler(DWORD ctrl_type) {
    (void)ctrl_type;
    if (g_stop_event) {
        SetEvent(g_stop_event);
        return TRUE;
    }
    return FALSE;
}

static DWORD WINAPI wait_for_enter_thread(LPVOID param) {
    (void)param;
    getchar();
    if (g_stop_event) {
        SetEvent(g_stop_event);
    }
    return 0;
}

int hfs_winfsp_mount(const wchar_t* device_or_image_path, const wchar_t* mount_point, bool debug_mode) {
    (void)debug_mode;

    hfs_block_device_t* dev = hfs_block_device_open_win32(device_or_image_path);
    if (!dev) {
        DWORD err = GetLastError();
        fwprintf(stderr, L"Failed to open device: %ls (Error Code: %lu)\n", device_or_image_path, err);
        if (err == ERROR_ACCESS_DENIED) {
            fwprintf(stderr, L"--> [ACCESS DENIED]: You MUST run Command Prompt or PowerShell AS ADMINISTRATOR to access raw physical disks.\n");
        } else if (err == ERROR_FILE_NOT_FOUND || err == ERROR_PATH_NOT_FOUND) {
            fwprintf(stderr, L"--> [NOT FOUND]: Device path not found. Check Disk Management (e.g. \\\\.\\PhysicalDrive3).\n");
        }
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
    InitializeCriticalSection(&ctx->lock);

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
    wcsncpy_s(params.FileSystemName, sizeof(params.FileSystemName)/sizeof(wchar_t), L"HFS+", _TRUNCATE);

    /* Ensure the WinFsp kernel driver service is started if possible */
    FspFsctlStartService();

    NTSTATUS status = FspFileSystemCreate((PWSTR)L"" FSP_FSCTL_DISK_DEVICE_NAME, &params, &g_hfs_interface, &ctx->fsp);
    if (!NT_SUCCESS(status)) {
        fwprintf(stderr, L"FspFileSystemCreate failed with status 0x%08X\n", (unsigned int)status);
        if ((unsigned int)status == 0xC000000E) {
            fwprintf(stderr, L"--> [WinFsp Kernel Driver Not Installed or Running]:\n");
            fwprintf(stderr, L"    The WinFsp core file system driver is required to mount volumes.\n");
            fwprintf(stderr, L"    1. If WinFsp is not installed, install it via Administrator terminal:\n");
            fwprintf(stderr, L"           winget install --id WinFsp.WinFsp\n");
            fwprintf(stderr, L"       or download MSI: https://github.com/winfsp/winfsp/releases\n");
            fwprintf(stderr, L"    2. You can check WinFsp driver status with:\n");
            fwprintf(stderr, L"           \"C:\\Program Files (x86)\\WinFsp\\bin\\fsptool-x64.exe\" lsdrv\n");
        }
        DeleteCriticalSection(&ctx->lock);
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
        DeleteCriticalSection(&ctx->lock);
        hfs_free(ctx);
        hfs_catalog_close(cat);
        hfs_volume_close(vol);
        hfs_block_device_close(dev);
        return -1;
    }

    wchar_t raw_target[512] = {0};
    wchar_t global_dev[64] = {0};
    bool created_global_dos_device = false;

    if (mount_point[1] == L':' && (mount_point[2] == L'\0' || (mount_point[2] == L'\\' && mount_point[3] == L'\0'))) {
        wchar_t drive_letter[3] = { mount_point[0], L':', L'\0' };
        if (QueryDosDeviceW(drive_letter, raw_target, sizeof(raw_target)/sizeof(wchar_t)) > 0) {
            swprintf_s(global_dev, sizeof(global_dev)/sizeof(wchar_t), L"Global\\%ls", drive_letter);
            if (DefineDosDeviceW(DDD_RAW_TARGET_PATH, global_dev, raw_target)) {
                created_global_dos_device = true;
                wprintf(L"[WinFsp] Published %ls globally to all Windows sessions.\n", drive_letter);
            }
        }
        wchar_t root_path[4] = { mount_point[0], L':', L'\\', L'\0' };
        SHChangeNotify(SHCNE_DRIVEADD, SHCNF_PATHW, root_path, NULL);
    }

    status = FspFileSystemStartDispatcher(ctx->fsp, 1);
    if (!NT_SUCCESS(status)) {
        fwprintf(stderr, L"FspFileSystemStartDispatcher failed with status 0x%08X\n", (unsigned int)status);
        FspFileSystemDelete(ctx->fsp);
        DeleteCriticalSection(&ctx->lock);
        hfs_free(ctx);
        hfs_catalog_close(cat);
        hfs_volume_close(vol);
        hfs_block_device_close(dev);
        return -1;
    }

    g_stop_event = CreateEventW(NULL, TRUE, FALSE, NULL);
    SetConsoleCtrlHandler(console_ctrl_handler, TRUE);

    HANDLE h_thread = CreateThread(NULL, 0, wait_for_enter_thread, NULL, 0, NULL);

    wprintf(L"\n=======================================================\n");
    wprintf(L"  HFS+ volume successfully mounted at [%ls]!\n", mount_point);
    wprintf(L"  - Open Windows Explorer ('This PC') -> %ls\n", mount_point);
    wprintf(L"  - In Command Prompt / PowerShell, verify with:  dir %ls\\\n", mount_point);
    wprintf(L"  Press Enter or Ctrl+C to unmount and exit.\n");
    wprintf(L"=======================================================\n\n");

    WaitForSingleObject(g_stop_event, INFINITE);

    wprintf(L"\nUnmounting volume [%ls]...\n", mount_point);
    if (created_global_dos_device) {
        DefineDosDeviceW(DDD_REMOVE_DEFINITION | DDD_RAW_TARGET_PATH, global_dev, raw_target);
        wchar_t root_path[4] = { mount_point[0], L':', L'\\', L'\0' };
        SHChangeNotify(SHCNE_DRIVEREMOVED, SHCNF_PATHW, root_path, NULL);
    }
    FspFileSystemStopDispatcher(ctx->fsp);
    FspFileSystemDelete(ctx->fsp);
    hfs_catalog_close(cat);
    hfs_volume_close(vol);
    hfs_block_device_close(dev);
    DeleteCriticalSection(&ctx->lock);
    hfs_free(ctx);

    if (h_thread) {
        CloseHandle(h_thread);
    }
    if (g_stop_event) {
        CloseHandle(g_stop_event);
        g_stop_event = NULL;
    }

    if (g_default_sd) {
        LocalFree(g_default_sd);
        g_default_sd = NULL;
    }

    wprintf(L"Volume unmounted successfully.\n");
    return 0;
}

#endif /* _WIN32 */
