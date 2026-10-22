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
static volatile LONG ndis_bind_open_status;
static volatile LONG ndis_bind_open_error;
static volatile LONG ndis_unbind_calls;
static volatile LONG ndis_bind_successes;
static volatile LONG ndis_close_calls;
static KSPIN_LOCK ndis_lock;
static volatile LONG bind_pending;
static NDIS_HANDLE pending_bind_context;
static WCHAR retry_name_buffer[256];
static UNICODE_STRING retry_name;
static NDIS_HANDLE retry_protocol;
static int adapter_context;
static NDIS_HANDLE packet_pool;
static NDIS_HANDLE buffer_pool;
static UCHAR local_mac[6];
static volatile LONG rx_frames;
static volatile LONG tx_frames;
static volatile LONG arp_replies;
static KEVENT request_event;
static PNDIS_REQUEST active_request;
static NDIS_STATUS active_request_status;
static PNDIS_BUFFER last_send_buffer;

static VOID NTAPI open_complete(NDIS_HANDLE context, NDIS_STATUS status,
                                NDIS_STATUS open_error);

static VOID NTAPI retry_bind_thread(PVOID unused)
{
    LARGE_INTEGER delay;
    NDIS_STATUS status = NDIS_STATUS_FAILURE;
    NDIS_STATUS open_error = NDIS_STATUS_SUCCESS;
    NDIS_MEDIUM mediums[1] = { NdisMedium802_3 };
    UINT selected = 0;

    (void)unused;
    for (int attempt = 0; attempt < 20; attempt++) {
        delay.QuadPart = -10000000LL;   /* 1 second */
        KeDelayExecutionThread(KernelMode, FALSE, &delay);
        status = NDIS_STATUS_FAILURE;
        NdisOpenAdapter(&status, &open_error, &ndis_binding, &selected,
                        mediums, 1, retry_protocol, &adapter_context,
                        &retry_name, 0, NULL);
        InterlockedExchange(&ndis_open_status, (LONG)status);
        InterlockedExchange(&ndis_open_error, (LONG)open_error);
        InterlockedExchange(&ndis_bind_open_status, (LONG)status);
        InterlockedExchange(&ndis_bind_open_error, (LONG)open_error);
        if (status == NDIS_STATUS_SUCCESS || status == NDIS_STATUS_PENDING) {
            break;
        }
    }
    if (status == NDIS_STATUS_SUCCESS) {
        InterlockedIncrement(&ndis_bind_successes);
        InterlockedExchange(&ndis_bound, 1);
    } else if (status != NDIS_STATUS_PENDING) {
        InterlockedExchange(&ndis_bind_error, (LONG)status);
    }
    if (status != NDIS_STATUS_PENDING && InterlockedExchange(&bind_pending, 0)) {
        NdisCompleteBindAdapter(pending_bind_context, status, open_error);
    }
    PsTerminateSystemThread(STATUS_SUCCESS);
}

static VOID NTAPI open_complete(NDIS_HANDLE context, NDIS_STATUS status,
                                NDIS_STATUS open_error)
{
    (void)context;
    if (status == NDIS_STATUS_SUCCESS) {
        InterlockedExchange(&ndis_bound, 1);
        InterlockedIncrement(&ndis_bind_successes);
    }
    if (InterlockedExchange(&bind_pending, 0)) {
        NdisCompleteBindAdapter(pending_bind_context, status, open_error);
    }
}

static VOID NTAPI close_complete(NDIS_HANDLE context, NDIS_STATUS status)
{
    (void)context;
    (void)status;
    InterlockedIncrement(&ndis_close_calls);
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
    if (request == active_request) {
        active_request_status = status;
        KeSetEvent(&request_event, IO_NO_INCREMENT, FALSE);
    }
}

static NDIS_STATUS do_request(NDIS_REQUEST *request)
{
    NDIS_STATUS status;

    active_request = request;
    active_request_status = NDIS_STATUS_FAILURE;
    KeClearEvent(&request_event);
    NdisRequest(&status, ndis_binding, request);
    if (status == NDIS_STATUS_PENDING) {
        (void)KeWaitForSingleObject(&request_event, Executive, KernelMode,
                                    FALSE, NULL);
        status = active_request_status;
    }
    active_request = NULL;
    return status;
}

static void set_packet_filter(void)
{
    NDIS_REQUEST request;
    ULONG filter = NDIS_PACKET_TYPE_DIRECTED | NDIS_PACKET_TYPE_MULTICAST
                 | NDIS_PACKET_TYPE_BROADCAST | NDIS_PACKET_TYPE_PROMISCUOUS;

    memset(&request, 0, sizeof request);
    request.RequestType = NdisRequestSetInformation;
    request.DATA.SET_INFORMATION.Oid = OID_GEN_CURRENT_PACKET_FILTER;
    request.DATA.SET_INFORMATION.InformationBuffer = &filter;
    request.DATA.SET_INFORMATION.InformationBufferLength = sizeof filter;
    (void)do_request(&request);
}

static void query_mac(void)
{
    NDIS_REQUEST request;

    memset(&request, 0, sizeof request);
    request.RequestType = NdisRequestQueryInformation;
    request.DATA.QUERY_INFORMATION.Oid = OID_802_3_CURRENT_ADDRESS;
    request.DATA.QUERY_INFORMATION.InformationBuffer = local_mac;
    request.DATA.QUERY_INFORMATION.InformationBufferLength = sizeof local_mac;
    (void)do_request(&request);
}

static NDIS_STATUS NTAPI receive_packet(NDIS_HANDLE context, PNDIS_PACKET packet)
{
    PNDIS_BUFFER buffer = NULL;
    PVOID address = NULL;
    UINT length = 0;
    UINT total = 0;

    (void)context;
    NdisGetFirstBufferFromPacket(packet, &buffer, &address, &length, &total);
    if (address != NULL && total >= 14) {
        const unsigned char *frame = address;
        InterlockedIncrement(&rx_frames);
        if (total >= 42 && frame[12] == 0x08 && frame[13] == 0x06
            && frame[20] == 0x00 && frame[21] == 0x02) {
            InterlockedIncrement(&arp_replies);
        }
    }
    return NDIS_STATUS_SUCCESS;
}

static void send_arp_request(void)
{
    unsigned char frame[42] = {
        0xff,0xff,0xff,0xff,0xff,0xff,
        0,0,0,0,0,0,
        0x08,0x06,
        0x00,0x01, 0x08,0x00, 6, 4, 0x00,0x01,
        0,0,0,0,0,0,
        192,168,103,2,
        0,0,0,0,0,0,
        192,168,103,200
    };
    NDIS_STATUS status;
    PNDIS_PACKET packet = NULL;
    PNDIS_BUFFER buffer = NULL;

    memcpy(frame + 6, local_mac, 6);
    status = NdisAllocatePacket(&status, &packet, packet_pool);
    if (status != NDIS_STATUS_SUCCESS || packet == NULL) {
        return;
    }
    status = NdisAllocateBuffer(&status, &buffer, buffer_pool, frame,
                                (UINT)sizeof frame);
    if (status != NDIS_STATUS_SUCCESS || buffer == NULL) {
        NdisFreePacket(packet);
        return;
    }
    last_send_buffer = buffer;
    NdisChainBufferAtBack(packet, buffer);
    status = NdisSend(&status, ndis_binding, packet);
    if (status != NDIS_STATUS_PENDING) {
        NdisFreeBuffer(buffer);
        NdisFreePacket(packet);
        last_send_buffer = NULL;
    }
    InterlockedIncrement(&tx_frames);
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
    NDIS_STATUS open_status = NDIS_STATUS_FAILURE;

    (void)system_specific1;
    (void)system_specific2;
    InterlockedIncrement(&ndis_bind_calls);
    NdisOpenAdapter(&open_status, &open_error, &ndis_binding,
                    &selected, mediums, 1, ndis_protocol, &adapter_context,
                    device_name, 0, NULL);
    ndis_medium = mediums[0];
    InterlockedExchange(&ndis_open_status, (LONG)open_status);
    InterlockedExchange(&ndis_open_error, (LONG)open_error);
    InterlockedExchange(&ndis_bind_open_status, (LONG)open_status);
    InterlockedExchange(&ndis_bind_open_error, (LONG)open_error);
    if (open_status == NDIS_STATUS_SUCCESS) {
        InterlockedIncrement(&ndis_bind_successes);
        KeInitializeEvent(&request_event, NotificationEvent, FALSE);
        NdisAllocatePacketPool(&open_status, &packet_pool, 16,
                               sizeof(PVOID) * 2);
        NdisAllocateBufferPool(&open_status, &buffer_pool, 16);
        query_mac();
        set_packet_filter();
        InterlockedExchange(&ndis_bound, 1);
        *status = open_status;
        return open_status;
    }
    if (open_status != NDIS_STATUS_PENDING) {
        InterlockedExchange(&ndis_bind_error, (LONG)open_status);
    }
    /* CLOSING is transient during boot or netcfg Apply. Complete the bind
     * asynchronously after the adapter settles. */
    pending_bind_context = bind_context;
    retry_protocol = ndis_protocol;
    retry_name.Length = device_name->Length;
    retry_name.MaximumLength = device_name->Length + sizeof(WCHAR);
    retry_name.Buffer = retry_name_buffer;
    if (device_name->Length < sizeof retry_name_buffer - sizeof(WCHAR)) {
        memcpy(retry_name_buffer, device_name->Buffer, device_name->Length);
        retry_name_buffer[device_name->Length / sizeof(WCHAR)] = L'\0';
    }
    if (open_status == NDIS_STATUS_PENDING) {
        InterlockedExchange(&bind_pending, 1);
    } else {
        HANDLE thread = NULL;
        InterlockedExchange(&bind_pending, 1);
        (void)PsCreateSystemThread(&thread, 0, NULL, NULL, NULL,
                                   retry_bind_thread, NULL);
        if (thread != NULL) {
            ZwClose(thread);
        }
    }
    *status = NDIS_STATUS_PENDING;
    return NDIS_STATUS_PENDING;
}

static VOID NTAPI unbind_adapter(NDIS_HANDLE unbind_context,
                                 NDIS_HANDLE protocol_binding_context)
{
    NDIS_STATUS status;

    (void)unbind_context;
    (void)protocol_binding_context;
    InterlockedIncrement(&ndis_unbind_calls);
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

VOID anem_ndis_reenumerate(void)
{
    if (ndis_protocol != NULL) {
        NdisReEnumerateProtocolBindings(ndis_protocol);
    }
}

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
ULONG anem_ndis_bind_open_status(void) { return (ULONG)ndis_bind_open_status; }
ULONG anem_ndis_bind_open_error(void) { return (ULONG)ndis_bind_open_error; }
ULONG anem_ndis_unbind_calls(void) { return (ULONG)ndis_unbind_calls; }

void anem_ndis_traffic(ULONG out[4])
{
    LARGE_INTEGER delay;

    send_arp_request();
    delay.QuadPart = -5000000LL;    /* 500 ms for the reply */
    KeDelayExecutionThread(KernelMode, FALSE, &delay);
    out[0] = (ULONG)rx_frames;
    out[1] = (ULONG)tx_frames;
    out[2] = (ULONG)arp_replies;
    out[3] = (ULONG)ndis_bound;
}
ULONG anem_ndis_bind_successes(void) { return (ULONG)ndis_bind_successes; }
ULONG anem_ndis_close_calls(void) { return (ULONG)ndis_close_calls; }
