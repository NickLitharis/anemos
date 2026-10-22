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

#include "lwip/init.h"
#include "lwip/ip_addr.h"
#include "lwip/netif.h"
#include "lwip/pbuf.h"
#include "lwip/timeouts.h"
#include "lwip/udp.h"

#define ANEM_DEVICE_NAME L"\\Device\\AnemosNet"
#define ANEM_DOS_NAME L"\\DosDevices\\AnemosNet"
#define ANEM_PING_SIGNATURE 0x414E454DUL   /* 'ANEM' */

#define IOCTL_ANEMOS_PING \
    CTL_CODE(FILE_DEVICE_UNKNOWN, 0x800, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_ANEMOS_NET_SELFTEST \
    CTL_CODE(FILE_DEVICE_UNKNOWN, 0x801, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_ANEMOS_NDIS_STATE \
    CTL_CODE(FILE_DEVICE_UNKNOWN, 0x802, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_ANEMOS_NDIS_OPEN \
    CTL_CODE(FILE_DEVICE_UNKNOWN, 0x803, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_ANEMOS_NDIS_REENUM \
    CTL_CODE(FILE_DEVICE_UNKNOWN, 0x804, METHOD_BUFFERED, FILE_ANY_ACCESS)

NTSTATUS anem_ndis_start(void);
VOID anem_ndis_stop(void);
ULONG anem_ndis_is_bound(void);
ULONG anem_ndis_medium(void);
ULONG anem_ndis_error(void);
ULONG anem_ndis_bind_calls(void);
ULONG anem_ndis_open_status(void);
ULONG anem_ndis_open_error(void);
ULONG anem_ndis_bind_open_status(void);
ULONG anem_ndis_bind_open_error(void);
ULONG anem_ndis_unbind_calls(void);
ULONG anem_ndis_bind_successes(void);
ULONG anem_ndis_close_calls(void);
ULONG anem_ndis_open(const WCHAR *path, ULONG length);
VOID anem_ndis_reenumerate(void);

/* lwIP has no OS clock under NO_SYS. KeQueryInterruptTime counts 100 ns
 * units; shift and divide in 32 bits so no libgcc helper is needed. */
u32_t sys_now(void)
{
    return (u32_t)((KeQueryInterruptTime() >> 4) / 625u);
}

static unsigned char selftest_payload[32];
static u16_t selftest_length;
static int selftest_received;

static void on_selftest_udp(void *arg, struct udp_pcb *pcb, struct pbuf *p,
                            const ip_addr_t *address, u16_t port)
{
    (void)arg;
    (void)pcb;
    (void)address;
    (void)port;
    if (p != NULL) {
        u16_t copy = p->tot_len < sizeof selftest_payload
                   ? p->tot_len : (u16_t)sizeof selftest_payload;
        (void)pbuf_copy_partial(p, selftest_payload, copy, 0);
        selftest_length = copy;
        selftest_received = 1;
        pbuf_free(p);
    }
}

static ULONG fnv1a(const unsigned char *data, u16_t length)
{
    ULONG hash = 2166136261UL;

    for (u16_t i = 0; i < length; i++) {
        hash ^= data[i];
        hash *= 16777619UL;
    }
    return hash;
}

/* K2: prove lwIP runs in kernel context. A UDP datagram goes through the
 * built-in loopback netif and comes back to a bound PCB; the IOCTL returns a
 * checksum of the received bytes so the user-mode test can compare it with an
 * independent implementation. */
static ULONG run_lwip_selftest(void)
{
    static int lwip_started;
    struct netif *loop = NULL;
    struct udp_pcb *pcb;
    struct pbuf *packet;
    ip_addr_t destination;
    const char *text = "anemos-kernel";
    u16_t length = (u16_t)strlen(text);

    if (!lwip_started) {
        lwip_init();
        lwip_started = 1;
    }
    for (struct netif *walk = netif_list; walk != NULL; walk = walk->next) {
        if (walk->name[0] == 'l' && walk->name[1] == 'o' && netif_is_up(walk)) {
            loop = walk;
            break;
        }
    }
    if (loop == NULL) {
        return 0;
    }
    pcb = udp_new();
    if (pcb == NULL) {
        return 0;
    }
    IP_ADDR4(&destination, 127, 0, 0, 1);
    if (udp_bind(pcb, &destination, 5556) != ERR_OK) {
        udp_remove(pcb);
        return 0;
    }
    selftest_received = 0;
    selftest_length = 0;
    udp_recv(pcb, on_selftest_udp, NULL);
    packet = pbuf_alloc(PBUF_TRANSPORT, length, PBUF_RAM);
    if (packet == NULL || pbuf_take(packet, text, length) != ERR_OK) {
        if (packet != NULL) {
            pbuf_free(packet);
        }
        udp_remove(pcb);
        return 0;
    }
    if (udp_sendto(pcb, packet, &destination, 5556) != ERR_OK) {
        pbuf_free(packet);
        udp_remove(pcb);
        return 0;
    }
    pbuf_free(packet);
    netif_poll(loop);
    sys_check_timeouts();
    udp_remove(pcb);
    if (!selftest_received) {
        return 0;
    }
    return fnv1a(selftest_payload, selftest_length);
}

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
    if (stack->Parameters.DeviceIoControl.IoControlCode == IOCTL_ANEMOS_NDIS_REENUM) {
        anem_ndis_reenumerate();
        status = STATUS_SUCCESS;
    } else if (stack->Parameters.DeviceIoControl.IoControlCode == IOCTL_ANEMOS_NDIS_OPEN) {
        ULONG length = stack->Parameters.DeviceIoControl.InputBufferLength;
        if (length < sizeof(WCHAR) || length > 512) {
            status = STATUS_INVALID_PARAMETER;
        } else {
            ULONG answer = anem_ndis_open(
                (const WCHAR *)irp->AssociatedIrp.SystemBuffer, length);
            if (stack->Parameters.DeviceIoControl.OutputBufferLength >= sizeof(ULONG)) {
                *(ULONG *)irp->AssociatedIrp.SystemBuffer = answer;
                information = sizeof(ULONG);
            }
            status = STATUS_SUCCESS;
        }
    } else if (stack->Parameters.DeviceIoControl.IoControlCode == IOCTL_ANEMOS_NDIS_STATE) {
        if (stack->Parameters.DeviceIoControl.OutputBufferLength < 11 * sizeof(ULONG)) {
            status = STATUS_BUFFER_TOO_SMALL;
        } else {
            ULONG *out = irp->AssociatedIrp.SystemBuffer;
            out[0] = anem_ndis_is_bound();
            out[1] = anem_ndis_medium();
            out[2] = anem_ndis_error();
            out[3] = anem_ndis_bind_calls();
            out[4] = anem_ndis_open_status();
            out[5] = anem_ndis_open_error();
            out[6] = anem_ndis_bind_open_status();
            out[7] = anem_ndis_bind_open_error();
            out[8] = anem_ndis_unbind_calls();
            out[9] = anem_ndis_bind_successes();
            out[10] = anem_ndis_close_calls();
            information = 11 * sizeof(ULONG);
            status = STATUS_SUCCESS;
        }
    } else if (stack->Parameters.DeviceIoControl.IoControlCode == IOCTL_ANEMOS_PING
        || stack->Parameters.DeviceIoControl.IoControlCode == IOCTL_ANEMOS_NET_SELFTEST) {
        if (stack->Parameters.DeviceIoControl.OutputBufferLength < sizeof(ULONG)) {
            status = STATUS_BUFFER_TOO_SMALL;
        } else {
            ULONG answer = stack->Parameters.DeviceIoControl.IoControlCode
                                 == IOCTL_ANEMOS_PING
                         ? ANEM_PING_SIGNATURE : run_lwip_selftest();
            *(ULONG *)irp->AssociatedIrp.SystemBuffer = answer;
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
    anem_ndis_stop();
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
    (void)anem_ndis_start();
    return STATUS_SUCCESS;
}
