#include "hfsplus_kernel.h"

#ifdef _KERNEL_MODE

static PDEVICE_OBJECT g_ControlDeviceObject = NULL;

VOID NTAPI HfsUnload(PDRIVER_OBJECT DriverObject) {
    UNICODE_STRING dosDeviceName;
    RtlInitUnicodeString(&dosDeviceName, HFS_DOS_DEVICE_NAME);
    IoDeleteSymbolicLink(&dosDeviceName);

    if (g_ControlDeviceObject) {
        IoUnregisterFileSystem(g_ControlDeviceObject);
        IoDeleteDevice(g_ControlDeviceObject);
        g_ControlDeviceObject = NULL;
    }
}

NTSTATUS NTAPI DriverEntry(PDRIVER_OBJECT DriverObject, PUNICODE_STRING RegistryPath) {
    UNREFERENCED_PARAMETER(RegistryPath);

    NTSTATUS status;
    UNICODE_STRING devName, dosDeviceName;

    RtlInitUnicodeString(&devName, HFS_DEVICE_NAME);
    RtlInitUnicodeString(&dosDeviceName, HFS_DOS_DEVICE_NAME);

    status = IoCreateDevice(DriverObject, 0, &devName, FILE_DEVICE_DISK_FILE_SYSTEM,
                            0, FALSE, &g_ControlDeviceObject);
    if (!NT_SUCCESS(status)) {
        return status;
    }

    status = IoCreateSymbolicLink(&dosDeviceName, &devName);
    if (!NT_SUCCESS(status)) {
        IoDeleteDevice(g_ControlDeviceObject);
        return status;
    }

    DriverObject->DriverUnload = HfsUnload;

    DriverObject->MajorFunction[IRP_MJ_CREATE]                  = HfsCreate;
    DriverObject->MajorFunction[IRP_MJ_CLOSE]                   = HfsClose;
    DriverObject->MajorFunction[IRP_MJ_CLEANUP]                 = HfsCleanup;
    DriverObject->MajorFunction[IRP_MJ_READ]                    = HfsRead;
    DriverObject->MajorFunction[IRP_MJ_FILE_SYSTEM_CONTROL]     = HfsFileSystemControl;
    DriverObject->MajorFunction[IRP_MJ_DIRECTORY_CONTROL]       = HfsDirectoryControl;
    DriverObject->MajorFunction[IRP_MJ_QUERY_INFORMATION]       = HfsQueryInformation;
    DriverObject->MajorFunction[IRP_MJ_QUERY_VOLUME_INFORMATION]= HfsQueryVolumeInformation;

    /* Register the file system with the I/O Manager */
    IoRegisterFileSystem(g_ControlDeviceObject);

    return STATUS_SUCCESS;
}

#endif /* _KERNEL_MODE */
