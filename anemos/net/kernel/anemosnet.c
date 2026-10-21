/* Anemos network driver — K1 skeleton.
 *
 * Creates a device and a DOS symbolic link, answers one IOCTL with a fixed
 * signature, and unloads cleanly. It proves the toolchain (clang + mingw DDK
 * headers + generated ntoskrnl import library), the XP image rules below, and
 * the guest deployment path before any packet code.
 *
 * XP image rules learned here, all verified in the guest:
 * - the PE checksum is checked when a driver loads, so a zero or stale
 *   checksum fails with STATUS_IMAGE_CHECKSUM_MISMATCH;
 * - Characteristics 0x10E, SectionAlignment/FileAlignment 0x80 and subsystem
 *   5.01 match XP's own drivers;
 * - a .data section with zero raw size was rejected. Keeping the completion
 *   pointer initialized makes the section file-backed.
 */
#include <ntddk.h>

#define ANEM_DEVICE_NAME L"\\Device\\AnemosNet"
#define ANEM_DOS_NAME L"\\DosDevices\\AnemosNet"
#define ANEM_PING_SIGNATURE 0x414E454DUL   /* 'ANEM' */

#define IOCTL_ANEMOS_PING \
    CTL_CODE(FILE_DEVICE_UNKNOWN, 0x800, METHOD_BUFFERED, FILE_ANY_ACCESS)

/* IofCompleteRequest is FASTCALL, which the generated import library cannot
 * name without a decoration mismatch. Resolve it at runtime through
 * MmGetSystemRoutineAddress, which is stdcall and imports cleanly.
 *
 * The non-NULL initializer is deliberate: XP rejects this image when .data
 * has zero raw size, and an uninitialized pointer leaves it empty. */
typedef VOID (__attribute__((fastcall)) *PFN_COMPLETE)(PIRP, CCHAR);
static PFN_COMPLETE complete_irp_fn = (PFN_COMPLETE)(ULONG_PTR)1;

static VOID NTAPI resolve_complete_irp(void)
{
    UNICODE_STRING name;

    RtlInitUnicodeString(&name, L"IofCompleteRequest");
    complete_irp_fn = (PFN_COMPLETE)(void *)MmGetSystemRoutineAddress(&name);
}

static NTSTATUS NTAPI complete_irp(PIRP irp, NTSTATUS status, ULONG_PTR information)
{
    irp->IoStatus.Status = status;
    irp->IoStatus.Information = information;
    if (complete_irp_fn == (PFN_COMPLETE)(ULONG_PTR)1) {
        resolve_complete_irp();
    }
    if (complete_irp_fn == NULL || complete_irp_fn == (PFN_COMPLETE)(ULONG_PTR)1) {
        return STATUS_PROCEDURE_NOT_FOUND;
    }
    complete_irp_fn(irp, (CCHAR)IO_NO_INCREMENT);
    return status;
}

static NTSTATUS NTAPI dispatch_ok(PDEVICE_OBJECT device, PIRP irp)
{
    (void)device;
    return complete_irp(irp, STATUS_SUCCESS, 0);
}

static NTSTATUS NTAPI dispatch_control(PDEVICE_OBJECT device, PIRP irp)
{
    PIO_STACK_LOCATION stack = IoGetCurrentIrpStackLocation(irp);
    NTSTATUS status = STATUS_INVALID_DEVICE_REQUEST;
    ULONG_PTR information = 0;

    (void)device;
    if (stack->Parameters.DeviceIoControl.IoControlCode == IOCTL_ANEMOS_PING) {
        if (stack->Parameters.DeviceIoControl.OutputBufferLength < sizeof(ULONG)) {
            status = STATUS_BUFFER_TOO_SMALL;
        } else {
            *(ULONG *)irp->AssociatedIrp.SystemBuffer = ANEM_PING_SIGNATURE;
            information = sizeof(ULONG);
            status = STATUS_SUCCESS;
        }
    }
    return complete_irp(irp, status, information);
}

static VOID NTAPI driver_unload(PDRIVER_OBJECT driver)
{
    UNICODE_STRING link;

    RtlInitUnicodeString(&link, ANEM_DOS_NAME);
    IoDeleteSymbolicLink(&link);
    IoDeleteDevice(driver->DeviceObject);
}

NTSTATUS NTAPI DriverEntry(PDRIVER_OBJECT driver, PUNICODE_STRING registry_path)
{
    UNICODE_STRING name;
    UNICODE_STRING link;
    PDEVICE_OBJECT device = NULL;
    NTSTATUS status;

    (void)registry_path;
    RtlInitUnicodeString(&name, ANEM_DEVICE_NAME);
    status = IoCreateDevice(driver, 0, &name, FILE_DEVICE_UNKNOWN,
                            FILE_DEVICE_SECURE_OPEN, FALSE, &device);
    if (!NT_SUCCESS(status)) {
        return status;
    }
    RtlInitUnicodeString(&link, ANEM_DOS_NAME);
    status = IoCreateSymbolicLink(&link, &name);
    if (!NT_SUCCESS(status)) {
        IoDeleteDevice(device);
        return status;
    }

    driver->MajorFunction[IRP_MJ_CREATE] = dispatch_ok;
    driver->MajorFunction[IRP_MJ_CLOSE] = dispatch_ok;
    driver->MajorFunction[IRP_MJ_CLEANUP] = dispatch_ok;
    driver->MajorFunction[IRP_MJ_DEVICE_CONTROL] = dispatch_control;
    driver->DriverUnload = driver_unload;
    device->Flags |= DO_BUFFERED_IO;
    return STATUS_SUCCESS;
}
