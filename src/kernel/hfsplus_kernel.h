#ifndef HFSPLUS_KERNEL_H
#define HFSPLUS_KERNEL_H

#ifdef _KERNEL_MODE
#include <ntifs.h>
#include <ntddk.h>
#include <ntstrsafe.h>

#include "hfsplus/volume.h"
#include "hfsplus/catalog.h"

#define HFS_DEVICE_NAME L"\\Device\\HfsPlusFs"
#define HFS_DOS_DEVICE_NAME L"\\DosDevices\\HfsPlusFs"

typedef struct _HFS_VCB {
    DEVICE_OBJECT* VolumeDeviceObject;
    DEVICE_OBJECT* TargetDeviceObject;
    VPB* Vpb;

    ERESOURCE VcbResource;
    ERESOURCE PagingIoResource;

    hfs_volume_t* vol;
    hfs_catalog_t* cat;
    hfs_block_device_t* dev;

    ULONG OpenHandleCount;
    ULONG ReferenceCount;
    BOOLEAN ReadOnly;
} HFS_VCB, *PHFS_VCB;

typedef struct _HFS_FCB {
    FSRTL_ADVANCED_FCB_HEADER Header;
    SECTION_OBJECT_POINTERS SectionObjectPointers;
    ERESOURCE MainResource;
    ERESOURCE PagingIoResource;

    PHFS_VCB Vcb;
    hfs_dirent_t Dirent;

    ULONG ReferenceCount;
    ULONG OpenHandleCount;
} HFS_FCB, *PHFS_FCB;

typedef struct _HFS_CCB {
    PHFS_FCB Fcb;
    UNICODE_STRING SearchPattern;
    ULONG DirectoryOffset;
} HFS_CCB, *PHFS_CCB;

/* IRP Dispatch Prototypes */
NTSTATUS NTAPI HfsCreate(PDEVICE_OBJECT DeviceObject, PIRP Irp);
NTSTATUS NTAPI HfsClose(PDEVICE_OBJECT DeviceObject, PIRP Irp);
NTSTATUS NTAPI HfsCleanup(PDEVICE_OBJECT DeviceObject, PIRP Irp);
NTSTATUS NTAPI HfsRead(PDEVICE_OBJECT DeviceObject, PIRP Irp);
NTSTATUS NTAPI HfsFileSystemControl(PDEVICE_OBJECT DeviceObject, PIRP Irp);
NTSTATUS NTAPI HfsDirectoryControl(PDEVICE_OBJECT DeviceObject, PIRP Irp);
NTSTATUS NTAPI HfsQueryInformation(PDEVICE_OBJECT DeviceObject, PIRP Irp);
NTSTATUS NTAPI HfsQueryVolumeInformation(PDEVICE_OBJECT DeviceObject, PIRP Irp);

#endif /* _KERNEL_MODE */

#endif /* HFSPLUS_KERNEL_H */
