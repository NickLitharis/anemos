/* K3: attach the Anemos driver to the NIC as an NDIS 5.1 protocol driver.
 *
 * This first slice registers the protocol and opens the 802.3 adapter. It
 * reports whether the binding is up; frame receive/send follows once the
 * binding is known to work.
 */
#include "ndis_min.h"

#include <string.h>

static NDIS_HANDLE ndis_protocol;
static NDIS_HANDLE ndis_binding;
static NDIS_MEDIUM ndis_medium;
static volatile LONG ndis_bound;
static volatile LONG ndis_bind_error;
static volatile LONG ndis_bind_calls;
static volatile LONG ndis_open_status;
static volatile LONG ndis_open_error;
static KSPIN_LOCK ndis_lock;

static VOID NTAPI open_complete(NDIS_HANDLE context, NDIS_STATUS status,
                                NDIS_STATUS open_error)
{
    (void)context;
    (void)open_error;
    if (status == NDIS_STATUS_SUCCESS) {
        InterlockedExchange(&ndis_bound, 1);
    }
}

static VOID NTAPI close_complete(NDIS_HANDLE context, NDIS_STATUS status)
{
    (void)context;
    (void)status;
    InterlockedExchange(&ndis_bound, 0);
}

static VOID NTAPI send_complete(NDIS_HANDLE context, PNDIS_PACKET packet,
                                NDIS_STATUS status)
{
    (void)context;
    (void)packet;
    (void)status;
}

static VOID NTAPI transfer_complete(NDIS_HANDLE context, PNDIS_PACKET packet,
                                    PNDIS_BUFFER buffer, UINT length,
                                    UINT transfer_length)
{
    (void)context;
    (void)packet;
    (void)buffer;
    (void)length;
    (void)transfer_length;
}

static VOID NTAPI reset_complete(NDIS_HANDLE context, NDIS_STATUS status)
{
    (void)context;
    (void)status;
}

static VOID NTAPI request_complete(NDIS_HANDLE context, PNDIS_REQUEST request,
                                   NDIS_STATUS status)
{
    (void)context;
    (void)request;
    (void)status;
}

static NDIS_STATUS NTAPI receive_frame(NDIS_HANDLE context,
                                       NDIS_HANDLE mac_context,
                                       PVOID header_buffer,
                                       UINT header_size,
                                       PVOID lookahead_buffer,
                                       UINT lookahead_size,
                                       UINT packet_size)
{
    (void)context;
    (void)mac_context;
    (void)header_buffer;
    (void)header_size;
    (void)lookahead_buffer;
    (void)lookahead_size;
    (void)packet_size;
    return NDIS_STATUS_NOT_ACCEPTED;
}

static VOID NTAPI receive_complete(NDIS_HANDLE context)
{
    (void)context;
}

static VOID NTAPI status_change(NDIS_HANDLE context, NDIS_STATUS status,
                                PVOID buffer, UINT length)
{
    (void)context;
    (void)status;
    (void)buffer;
    (void)length;
}

static VOID NTAPI status_complete(NDIS_HANDLE context)
{
    (void)context;
}

static NDIS_STATUS NTAPI bind_adapter(PNDIS_STATUS status,
                                      NDIS_HANDLE bind_context,
                                      PNDIS_STRING device_name,
                                      PVOID system_specific1,
                                      PVOID system_specific2)
{
    NDIS_MEDIUM mediums[1] = { NdisMedium802_3 };
    UINT selected = 0;
    NDIS_STATUS open_error = NDIS_STATUS_SUCCESS;

    (void)system_specific1;
    (void)system_specific2;
    InterlockedIncrement(&ndis_bind_calls);
    NdisOpenAdapter(status, &open_error, &ndis_binding,
                    &selected, mediums, 1, bind_context, NULL,
                    device_name, 0, NULL);
    ndis_medium = mediums[0];
    InterlockedExchange(&ndis_open_status, (LONG)*status);
    InterlockedExchange(&ndis_open_error, (LONG)open_error);
    if (*status == NDIS_STATUS_SUCCESS) {
        InterlockedExchange(&ndis_bound, 1);
    } else if (*status != NDIS_STATUS_PENDING) {
        InterlockedExchange(&ndis_bind_error, (LONG)*status);
    }
    return *status;
}

static VOID NTAPI unbind_adapter(NDIS_HANDLE unbind_context,
                                 NDIS_HANDLE protocol_binding_context)
{
    NDIS_STATUS status;

    (void)unbind_context;
    (void)protocol_binding_context;
    if (ndis_binding != NULL) {
        NdisCloseAdapter(&status, ndis_binding);
        ndis_binding = NULL;
    }
    InterlockedExchange(&ndis_bound, 0);
}

static NDIS_STATUS NTAPI pnp_event(NDIS_HANDLE context,
                                   PNET_PNP_EVENT event)
{
    (void)context;
    (void)event;
    return NDIS_STATUS_SUCCESS;
}

static VOID NTAPI protocol_unload(VOID)
{
}

NTSTATUS anem_ndis_start(void)
{
    NDIS_STATUS status;
    NDIS_PROTOCOL_CHARACTERISTICS chars;
    static UNICODE_STRING name = RTL_CONSTANT_STRING(L"AnemosNet");

    memset(&chars, 0, sizeof chars);
    KeInitializeSpinLock(&ndis_lock);
    chars.MajorNdisVersion = 5;
    chars.MinorNdisVersion = 1;
    chars.Name = name;
    chars.OpenAdapterCompleteHandler = open_complete;
    chars.CloseAdapterCompleteHandler = close_complete;
    chars.SendCompleteHandler = send_complete;
    chars.TransferDataCompleteHandler = transfer_complete;
    chars.ResetCompleteHandler = reset_complete;
    chars.RequestCompleteHandler = request_complete;
    chars.ReceiveHandler = receive_frame;
    chars.ReceiveCompleteHandler = receive_complete;
    chars.StatusHandler = status_change;
    chars.StatusCompleteHandler = status_complete;
    chars.BindAdapterHandler = bind_adapter;
    chars.UnbindAdapterHandler = unbind_adapter;
    chars.PnPEventHandler = pnp_event;
    chars.UnloadHandler = protocol_unload;

    status = NdisRegisterProtocol(&status, &ndis_protocol, &chars,
                                  sizeof chars);
    return status == NDIS_STATUS_SUCCESS ? STATUS_SUCCESS
                                        : STATUS_UNSUCCESSFUL;
}

VOID anem_ndis_stop(void)
{
    NDIS_STATUS status;

    if (ndis_protocol != NULL) {
        NdisDeregisterProtocol(&status, ndis_protocol);
        ndis_protocol = NULL;
    }
}

ULONG anem_ndis_is_bound(void)
{
    return (ULONG)ndis_bound;
}

ULONG anem_ndis_medium(void)
{
    return (ULONG)ndis_medium;
}

ULONG anem_ndis_error(void)
{
    return (ULONG)ndis_bind_error;
}

ULONG anem_ndis_bind_calls(void) { return (ULONG)ndis_bind_calls; }

/* NDIS did not offer existing adapters to a demand-started protocol, so the
 * test supplies the adapter's device name (\Device\{GUID}) and we open it
 * directly, the same call BindAdapter would make. */
ULONG anem_ndis_open(const WCHAR *path, ULONG length)
{
    NDIS_STATUS status = NDIS_STATUS_FAILURE;
    NDIS_STATUS open_error = NDIS_STATUS_SUCCESS;
    NDIS_MEDIUM mediums[1] = { NdisMedium802_3 };
    UINT selected = 0;
    UNICODE_STRING name;

    name.Length = (USHORT)length;
    name.MaximumLength = (USHORT)(length + sizeof(WCHAR));
    name.Buffer = (PWSTR)path;
    InterlockedIncrement(&ndis_bind_calls);
    NdisOpenAdapter(&status, &open_error, &ndis_binding, &selected, mediums, 1,
                    ndis_protocol, NULL, &name, 0, NULL);
    ndis_medium = mediums[0];
    InterlockedExchange(&ndis_open_status, (LONG)status);
    InterlockedExchange(&ndis_open_error, (LONG)open_error);
    if (status == NDIS_STATUS_SUCCESS) {
        InterlockedExchange(&ndis_bound, 1);
    } else {
        InterlockedExchange(&ndis_bind_error, (LONG)status);
    }
    return (ULONG)status;
}
ULONG anem_ndis_open_status(void) { return (ULONG)ndis_open_status; }
ULONG anem_ndis_open_error(void) { return (ULONG)ndis_open_error; }
