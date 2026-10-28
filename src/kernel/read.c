#include "hfsplus_kernel.h"
#include "hfsplus/fork.h"

#ifdef _KERNEL_MODE

NTSTATUS NTAPI HfsRead(PDEVICE_OBJECT DeviceObject, PIRP Irp) {
    PIO_STACK_LOCATION irpSp = IoGetCurrentIrpStackLocation(Irp);
    PFILE_OBJECT fileObject = irpSp->FileObject;
    PHFS_VCB vcb = (PHFS_VCB)DeviceObject->DeviceExtension;

    if (!vcb) {
        Irp->IoStatus.Status = STATUS_INVALID_DEVICE_REQUEST;
        IoCompleteRequest(Irp, IO_NO_INCREMENT);
        return STATUS_INVALID_DEVICE_REQUEST;
    }

    ULONG length = irpSp->Parameters.Read.Length;
    LARGE_INTEGER byteOffset = irpSp->Parameters.Read.ByteOffset;

    if (length == 0) {
        Irp->IoStatus.Status = STATUS_SUCCESS;
        Irp->IoStatus.Information = 0;
        IoCompleteRequest(Irp, IO_NO_INCREMENT);
        return STATUS_SUCCESS;
    }

    /* Pass read down to storage stack or process via Cache Manager */
    IoSkipCurrentIrpStackLocation(Irp);
    return IoCallDriver(vcb->TargetDeviceObject, Irp);
}

NTSTATUS NTAPI HfsDirectoryControl(PDEVICE_OBJECT DeviceObject, PIRP Irp) {
    UNREFERENCED_PARAMETER(DeviceObject);
    Irp->IoStatus.Status = STATUS_SUCCESS;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    return STATUS_SUCCESS;
}

NTSTATUS NTAPI HfsQueryInformation(PDEVICE_OBJECT DeviceObject, PIRP Irp) {
    UNREFERENCED_PARAMETER(DeviceObject);
    Irp->IoStatus.Status = STATUS_SUCCESS;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    return STATUS_SUCCESS;
}

NTSTATUS NTAPI HfsQueryVolumeInformation(PDEVICE_OBJECT DeviceObject, PIRP Irp) {
    UNREFERENCED_PARAMETER(DeviceObject);
    Irp->IoStatus.Status = STATUS_SUCCESS;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    return STATUS_SUCCESS;
}

#endif /* _KERNEL_MODE */
