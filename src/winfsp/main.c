#include "hfsplus_winfsp.h"
#include "hfsplus/diag.h"
#include "hfsplus/block_device.h"
#include <stdio.h>
#include <stdbool.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#include <shellapi.h>

static void print_usage(const wchar_t* prog) {
    wprintf(L"HFS+ Driver for Windows 11 (WinFsp)\n");
    wprintf(L"Usage:\n");
    wprintf(L"  Mount volume  : %ls -v <device_or_image> -m <mount_point> [-d]\n", prog);
    wprintf(L"  Run diagnostics: %ls -v <device_or_image> --diag\n", prog);
    wprintf(L"\nExamples:\n");
    wprintf(L"  %ls -v \\\\.\\PhysicalDrive3 --diag\n", prog);
    wprintf(L"  %ls -v \\\\.\\PhysicalDrive3 -m H:\n", prog);
}

int main(int argc, char* argv[]) {
    (void)argc;
    (void)argv;

    setvbuf(stdout, NULL, _IONBF, 0);
    setvbuf(stderr, NULL, _IONBF, 0);

#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
#endif

    int wargc = 0;
    LPWSTR* wargv = CommandLineToArgvW(GetCommandLineW(), &wargc);
    if (!wargv) {
        fprintf(stderr, "Failed to parse command line arguments.\n");
        return 1;
    }

    const wchar_t* device_path = NULL;
    const wchar_t* mount_point = NULL;
    bool debug_mode = false;
    bool diag_mode = false;

    for (int i = 1; i < wargc; i++) {
        if ((wcscmp(wargv[i], L"-v") == 0 || wcscmp(wargv[i], L"-i") == 0) && i + 1 < wargc) {
            device_path = wargv[++i];
        } else if (wcscmp(wargv[i], L"-m") == 0 && i + 1 < wargc) {
            mount_point = wargv[++i];
        } else if (wcscmp(wargv[i], L"-d") == 0) {
            debug_mode = true;
        } else if (wcscmp(wargv[i], L"--diag") == 0 || wcscmp(wargv[i], L"-diag") == 0) {
            diag_mode = true;
        } else if (wcscmp(wargv[i], L"-h") == 0 || wcscmp(wargv[i], L"--help") == 0) {
            print_usage(wargv[0]);
            LocalFree(wargv);
            return 0;
        } else if (!device_path && wargv[i][0] != L'-') {
            device_path = wargv[i];
        }
    }

    if (diag_mode) {
        if (!device_path) {
            fprintf(stderr, "Error: Specify device path to diagnose, e.g.:\n");
            fprintf(stderr, "       hfsplus.exe -v \\\\.\\PhysicalDrive3 --diag\n");
            LocalFree(wargv);
            return 1;
        }

        hfs_block_device_t* dev = hfs_block_device_open_win32(device_path);
        if (!dev) {
            DWORD err = GetLastError();
            fwprintf(stderr, L"Failed to open device: %ls (Error Code: %lu)\n", device_path, err);
            if (err == ERROR_ACCESS_DENIED) {
                fwprintf(stderr, L"--> [ACCESS DENIED]: Please run Command Prompt or PowerShell AS ADMINISTRATOR.\n");
            }
            LocalFree(wargv);
            return 1;
        }

        int res = hfs_run_diagnostics(dev);
        hfs_block_device_close(dev);
        LocalFree(wargv);
        return res;
    }

    if (!NT_SUCCESS(FspLoad(0))) {
        fprintf(stderr, "Error: Failed to load WinFsp runtime DLL (winfsp-x64.dll).\n");
        fprintf(stderr, "Please ensure WinFsp is installed from https://winfsp.dev/\n");
        LocalFree(wargv);
        return 1;
    }

    if (!device_path || !mount_point) {
        print_usage(wargv[0]);
        LocalFree(wargv);
        return 1;
    }

    wprintf(L"Initializing HFS+ WinFsp Driver...\n");
    int res = hfs_winfsp_mount(device_path, mount_point, debug_mode);
    LocalFree(wargv);
    return res;
}

#else

int main(int argc, char* argv[]) {
    if (argc < 2) {
        printf("HFS+ Diagnostics Tool\n");
        printf("Usage: hfsplus --diag <image_path>\n");
        return 1;
    }

    const char* path = argv[1];
    if (strcmp(path, "--diag") == 0 && argc >= 3) {
        path = argv[2];
    } else if (argc >= 3 && strcmp(argv[2], "--diag") == 0) {
        path = argv[1];
    }

    hfs_block_device_t* dev = hfs_block_device_open_file(path);
    if (!dev) {
        printf("Failed to open file: %s\n", path);
        return 1;
    }

    int res = hfs_run_diagnostics(dev);
    hfs_block_device_close(dev);
    return res;
}

#endif
