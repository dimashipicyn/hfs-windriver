#include "hfsplus_winfsp.h"
#include <stdio.h>
#include <stdbool.h>

#ifdef _WIN32
#include <windows.h>

static void print_usage(const wchar_t* prog) {
    wprintf(L"HFS+ Driver for Windows 11 (WinFsp)\n");
    wprintf(L"Usage:\n");
    wprintf(L"  %ls -v <device_or_image> -m <mount_point> [-d]\n", prog);
    wprintf(L"Examples:\n");
    wprintf(L"  %ls -v \\\\.\\PhysicalDrive1 -m H:\n", prog);
    wprintf(L"  %ls -v C:\\Images\\MacOS_Disk.dmg -m M:\n", prog);
}

int wmain(int argc, wchar_t* argv[]) {
    const wchar_t* device_path = NULL;
    const wchar_t* mount_point = NULL;
    bool debug_mode = false;

    for (int i = 1; i < argc; i++) {
        if ((wcscmp(argv[i], L"-v") == 0 || wcscmp(argv[i], L"-i") == 0) && i + 1 < argc) {
            device_path = argv[++i];
        } else if (wcscmp(argv[i], L"-m") == 0 && i + 1 < argc) {
            mount_point = argv[++i];
        } else if (wcscmp(argv[i], L"-d") == 0) {
            debug_mode = true;
        } else if (wcscmp(argv[i], L"-h") == 0 || wcscmp(argv[i], L"--help") == 0) {
            print_usage(argv[0]);
            return 0;
        }
    }

    if (!device_path || !mount_point) {
        print_usage(argv[0]);
        return 1;
    }

    wprintf(L"Initializing HFS+ WinFsp Driver...\n");
    return hfs_winfsp_mount(device_path, mount_point, debug_mode);
}

#else

int main(void) {
    printf("The WinFsp driver frontend is compiled for Windows.\n");
    printf("On macOS/Linux, use the core test harness or libhfsplus directly.\n");
    return 0;
}

#endif
