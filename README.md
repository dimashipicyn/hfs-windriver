# HFS+ File System Driver for Windows 11 (C11)

A modular, high-performance, and crash-safe HFS+ (Mac OS Extended / HFSX) file system driver for **Windows 11 (x64 and ARM64)** written in **pure C (C11)**.

---

## Architecture Overview

```
                      +---------------------------------------+
                      |       Windows Explorer / User I/O     |
                      +---------------------------------------+
                                          |
                      +---------------------------------------+
                      |  WinFsp (Windows File System Proxy)   |
                      |         winfsp-x64 / winfsp-aarch64   |
                      +---------------------------------------+
                                          |
                               [ hfsplus-winfsp.exe ]
                                          |
+-----------------------------------------------------------------------------------+
|                            libhfsplus (Pure C11 Core)                             |
|                                                                                   |
|  +--------------------+  +----------------------+  +---------------------------+  |
|  |   Volume Header    |  |     B-Tree Engine    |  |      Catalog & CNID       |  |
|  |  Superblock & Fork |  |  Index, Leaf, Search |  |  Path lookup & Directory  |  |
|  +--------------------+  +----------------------+  +---------------------------+  |
|  +--------------------+  +----------------------+  +---------------------------+  |
|  |  Extents Overflow  |  |   Unicode Collation  |  |   Block Device Abstraction|  |
|  |   Multi-extent map |  |   UTF-16BE <-> UTF-8 |  |   Raw Disk / File / Image |  |
|  +--------------------+  +----------------------+  +---------------------------+  |
+-----------------------------------------------------------------------------------+
                                          |
               +-----------------------------------------------------+
               |   Physical Drives / GPT Partitions / .dmg Images    |
               +-----------------------------------------------------+
```

### Components

1. **`libhfsplus` (`include/hfsplus/`, `src/core/`)**:
   - Independent C11 implementation of Apple Technical Note TN1150.
   - Zero external dependencies.
   - Can be embedded directly into user-mode tools, test runners, WinFsp services, or native NT kernel drivers (`.sys`).
   - Abstract memory model (`hfs_alloc`/`hfs_free`) maps to `malloc` in user-space and `ExAllocatePool2` in kernel mode.

2. **WinFsp Driver (`src/winfsp/`)**:
   - Mounts any HFS+ drive letter (e.g. `H:`) in Windows 11 File Explorer.
   - Crash-immune (runs in user mode, impossible to BSOD Windows 11).
   - Works with Windows 11 Memory Integrity (HVCI) and Secure Boot without requiring test signing.

3. **Kernel IFS Driver (`src/kernel/`)**:
   - Native Installable File System (IFS) driver skeleton for WDK.

---

## Building and Running Tests on macOS

The core engine and test suite can be built and tested directly on macOS using Clang and CMake:

```bash
# 1. Configure and build with AddressSanitizer and UBSan
cmake -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build

# 2. Run unit tests
./build/tests/test_volume

# 3. Test on a real macOS-generated HFS+ image
hdiutil create -size 16m -fs "HFS+" -volname "TestFixture" -layout NONE /tmp/test.dmg
./build/tests/test_catalog /tmp/test.dmg
rm /tmp/test.dmg
```

---

## Building for Windows 11

### Prerequisites
- Visual Studio 2022 (Community or higher) with C++ Desktop Development.
- [WinFsp](https://winfsp.dev/) installed (developer headers and libraries).

### Build Command (x64 / ARM64)
```cmd
cmake -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
```

### Mounting a Drive on Windows 11
```cmd
:: Mount a raw disk partition to drive H:
hfsplus.exe -v \\.\PhysicalDrive1 -m H:

:: Mount a disk image file to drive M:
hfsplus.exe -v C:\Images\MacBackup.dmg -m M:
```
