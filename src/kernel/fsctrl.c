#include "hfsplus_kernel.h"
#include "hfsplus/hfsplus_endian.h"

#ifdef _KERNEL_MODE

static NTSTATUS HfsMountVolume(PDEVICE_OBJECT DeviceObject, PIRP Irp) {
    PIO_STACK_LOCATION irpSp = IoGetCurrentIrpStackLocation(Irp);
    PDEVICE_OBJECT targetDevice = irpSp->Parameters.MountVolume.DeviceObject;
    PVPB vpb = irpSp->Parameters.MountVolume.Vpb;

    /* Read sector 2 (offset 1024) to probe for HFS+ Volume Header */
    LARGE_INTEGER offset;
    offset.QuadPart = 1024;

    uint8_t headerBuffer[512];
    KEVENT event;
    KeInitializeEvent(&event, NotificationEvent, FALSE);
    IO_STATUS_BLOCK ioStatus;

    PIRP readIrp = IoBuildSynchronousFsdRequest(IRP_MJ_READ, targetDevice,
                                               headerBuffer, sizeof(headerBuffer),
                                               &offset, &event, &ioStatus);
    if (!readIrp) {
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    NTSTATUS status = IoCallDriver(targetDevice, readIrp);
    if (status == STATUS_PENDING) {
        KeWaitForSingleObject(&event, Executive, KernelMode, FALSE, NULL);
        status = ioStatus.Status;
    }

    if (!NT_SUCCESS(status)) {
        return STATUS_UNRECOGNIZED_VOLUME;
    }

    const HFSPlusVolumeHeader* vh = (const HFSPlusVolumeHeader*)headerBuffer;
    uint16_t sig = be16_to_cpu(vh->signature);
    if (sig != kHFSPlusSigWord && sig != kHFSXSigWord) {
        return STATUS_UNRECOGNIZED_VOLUME;
    }

    /* Signature matched! Create Volume Device Object */
    PDEVICE_OBJECT volDeviceObject = NULL;
    status = IoCreateDevice(DeviceObject->DriverObject, sizeof(HFS_VCB), NULL,
                            FILE_DEVICE_DISK_FILE_SYSTEM, 0, FALSE, &volDeviceObject);
    if (!NT_SUCCESS(status)) {
        return status;
    }

    PHFS_VCB vcb = (PHFS_VCB)volDeviceObject->DeviceExtension;
    RtlZeroMemory(vcb, sizeof(HFS_VCB));
    vcb->VolumeDeviceObject = volDeviceObject;
    vcb->TargetDeviceObject = targetDevice;
    vcb->Vpb = vpb;
    vcb->ReadOnly = TRUE;

    ExInitializeResourceLite(&vcb->VcbResource);
    ExInitializeResourceLite(&vcb->PagingIoResource);

    volDeviceObject->Flags &= ~DO_DEVICE_INITIALIZING;
    vpb->DeviceObject = volDeviceObject;

    Irp->IoStatus.Status = STATUS_SUCCESS;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    return STATUS_SUCCESS;
}

NTSTATUS NTAPI HfsFileSystemControl(PDEVICE_OBJECT DeviceObject, PIRP Irp) {
    PIO_STACK_LOCATION irpSp = IoGetCurrentIrpStackLocation(Irp);

    switch (irpSp->MinorFunction) {
        case IRP_MN_MOUNT_VOLUME:
            return HfsMountVolume(DeviceObject, Irp);
        default:
            Irp->IoStatus.Status = STATUS_INVALID_DEVICE_REQUEST;
            IoCompleteRequest(Irp, IO_NO_INCREMENT);
            return STATUS_INVALID_DEVICE_REQUEST;
    }
}

#endif /* _KERNEL_MODE */
