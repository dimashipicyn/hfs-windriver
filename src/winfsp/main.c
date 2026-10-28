#include "hfsplus_winfsp.h"
#include <stdio.h>
#include <stdbool.h>

#ifdef _WIN32
#include <windows.h>
#include <shellapi.h>

static void print_usage(const wchar_t* prog) {
    wprintf(L"HFS+ Driver for Windows 11 (WinFsp)\n");
    wprintf(L"Usage:\n");
    wprintf(L"  %ls -v <device_or_image> -m <mount_point> [-d]\n", prog);
    wprintf(L"Examples:\n");
    wprintf(L"  %ls -v \\\\.\\PhysicalDrive1 -m H:\n", prog);
    wprintf(L"  %ls -v C:\\Images\\MacOS_Disk.dmg -m M:\n", prog);
}

int main(int argc, char* argv[]) {
    (void)argc;
    (void)argv;

    setvbuf(stdout, NULL, _IONBF, 0);
    setvbuf(stderr, NULL, _IONBF, 0);

#ifdef _WIN32
    if (!NT_SUCCESS(FspLoad(0))) {
        fprintf(stderr, "Error: Failed to load WinFsp runtime DLL (winfsp-x64.dll).\n");
        fprintf(stderr, "Please ensure WinFsp is installed from https://winfsp.dev/\n");
        return 1;
    }
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

    for (int i = 1; i < wargc; i++) {
        if ((wcscmp(wargv[i], L"-v") == 0 || wcscmp(wargv[i], L"-i") == 0) && i + 1 < wargc) {
            device_path = wargv[++i];
        } else if (wcscmp(wargv[i], L"-m") == 0 && i + 1 < wargc) {
            mount_point = wargv[++i];
        } else if (wcscmp(wargv[i], L"-d") == 0) {
            debug_mode = true;
        } else if (wcscmp(wargv[i], L"-h") == 0 || wcscmp(wargv[i], L"--help") == 0) {
            print_usage(wargv[0]);
            LocalFree(wargv);
            return 0;
        }
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

int main(void) {
    printf("The WinFsp driver frontend is compiled for Windows.\n");
    printf("On macOS/Linux, use the core test harness or libhfsplus directly.\n");
    return 0;
}

#endif
