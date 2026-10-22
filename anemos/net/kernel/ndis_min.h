/* Minimal NDIS 5.1 protocol ABI.
 *
 * The mingw DDK's ndis.h closes its include guard in the middle of the file
 * (line 2651), so including it twice redefines NDIS_REQUEST_TYPE. These are
 * the declarations this driver uses, with XP's field order.
 */
#ifndef ANEM_NDIS_MIN_H
#define ANEM_NDIS_MIN_H

#include <ntddk.h>

typedef int NDIS_STATUS;
typedef int *PNDIS_STATUS;
typedef void *NDIS_HANDLE;
typedef void **PNDIS_HANDLE;
typedef unsigned int UINT;
typedef UINT *PUINT;
#ifndef _PSTRING_DEFINED
typedef ANSI_STRING *PSTRING;
#endif
typedef UNICODE_STRING NDIS_STRING;
typedef UNICODE_STRING *PNDIS_STRING;
#define NDIS_STATUS_SUCCESS ((NDIS_STATUS)STATUS_SUCCESS)
#define NDIS_STATUS_PENDING ((NDIS_STATUS)STATUS_PENDING)
#define NDIS_STATUS_NOT_ACCEPTED ((NDIS_STATUS)0x00010003L)
#define NDIS_STATUS_FAILURE ((NDIS_STATUS)STATUS_UNSUCCESSFUL)

typedef enum _NDIS_MEDIUM {
    NdisMedium802_3 = 0,
    NdisMedium802_5,
    NdisMediumFddi,
    NdisMediumWan,
    NdisMediumLocalTalk,
    NdisMediumDix,
    NdisMediumArcnetRaw,
    NdisMediumArcnet878_2,
    NdisMediumAtm,
    NdisMediumWirelessWan,
    NdisMediumIrda,
    NdisMediumBpc,
    NdisMediumCoWan,
    NdisMedium1394,
    NdisMediumInfiniBand,
    NdisMediumTunnel,
    NdisMediumNative802_11,
    NdisMediumLoopback
} NDIS_MEDIUM, *PNDIS_MEDIUM;

typedef struct _NDIS_PACKET NDIS_PACKET, *PNDIS_PACKET;
typedef struct _NDIS_REQUEST NDIS_REQUEST, *PNDIS_REQUEST;
typedef struct _NET_PNP_EVENT NET_PNP_EVENT, *PNET_PNP_EVENT;
typedef struct _NDIS_BUFFER NDIS_BUFFER, *PNDIS_BUFFER;

typedef ULONG NDIS_OID;
typedef enum _NDIS_REQUEST_TYPE {
    NdisRequestQueryInformation,
    NdisRequestSetInformation,
    NdisRequestQueryStatistics,
    NdisRequestOpen,
    NdisRequestClose,
    NdisRequestSend,
    NdisRequestTransferData,
    NdisRequestReset,
    NdisRequestGeneric1,
} NDIS_REQUEST_TYPE;

typedef struct _NDIS_REQUEST {
    UCHAR MacReserved[4 * sizeof(PVOID)];
    NDIS_REQUEST_TYPE RequestType;
    union {
        struct {
            NDIS_OID Oid;
            PVOID InformationBuffer;
            UINT InformationBufferLength;
            UINT BytesWritten;
            UINT BytesNeeded;
        } SET_INFORMATION;
        struct {
            NDIS_OID Oid;
            PVOID InformationBuffer;
            UINT InformationBufferLength;
            UINT BytesRead;
            UINT BytesNeeded;
        } QUERY_INFORMATION;
    } DATA;
} NDIS_REQUEST, *PNDIS_REQUEST;

#define OID_GEN_CURRENT_PACKET_FILTER 0x0001010EUL
#define OID_802_3_CURRENT_ADDRESS 0x01010102UL
#define NDIS_PACKET_TYPE_DIRECTED 0x00000001UL
#define NDIS_PACKET_TYPE_MULTICAST 0x00000002UL
#define NDIS_PACKET_TYPE_BROADCAST 0x00000004UL
#define NDIS_PACKET_TYPE_PROMISCUOUS 0x00000020UL

typedef VOID (NTAPI *OPEN_ADAPTER_COMPLETE_HANDLER)(NDIS_HANDLE, NDIS_STATUS, NDIS_STATUS);
typedef VOID (NTAPI *CLOSE_ADAPTER_COMPLETE_HANDLER)(NDIS_HANDLE, NDIS_STATUS);
typedef VOID (NTAPI *SEND_COMPLETE_HANDLER)(NDIS_HANDLE, PNDIS_PACKET, NDIS_STATUS);
typedef VOID (NTAPI *TRANSFER_DATA_COMPLETE_HANDLER)(NDIS_HANDLE, PNDIS_PACKET, PNDIS_BUFFER, UINT, UINT);
typedef VOID (NTAPI *RESET_COMPLETE_HANDLER)(NDIS_HANDLE, NDIS_STATUS);
typedef VOID (NTAPI *REQUEST_COMPLETE_HANDLER)(NDIS_HANDLE, PNDIS_REQUEST, NDIS_STATUS);
typedef NDIS_STATUS (NTAPI *RECEIVE_HANDLER)(NDIS_HANDLE, NDIS_HANDLE, PVOID, UINT, PVOID, UINT, UINT);
typedef VOID (NTAPI *RECEIVE_COMPLETE_HANDLER)(NDIS_HANDLE);
typedef VOID (NTAPI *STATUS_HANDLER)(NDIS_HANDLE, NDIS_STATUS, PVOID, UINT);
typedef VOID (NTAPI *STATUS_COMPLETE_HANDLER)(NDIS_HANDLE);
typedef NDIS_STATUS (NTAPI *BIND_HANDLER)(PNDIS_STATUS, NDIS_HANDLE, PNDIS_STRING, PVOID, PVOID);
typedef VOID (NTAPI *UNBIND_HANDLER)(NDIS_HANDLE, NDIS_HANDLE);
typedef NDIS_STATUS (NTAPI *PNP_EVENT_HANDLER)(NDIS_HANDLE, PNET_PNP_EVENT);
typedef VOID (NTAPI *UNLOAD_PROTOCOL_HANDLER)(VOID);

typedef struct _NDIS_PROTOCOL_CHARACTERISTICS {
    UCHAR MajorNdisVersion;
    UCHAR MinorNdisVersion;
    USHORT Filler;
    union {
        UINT Reserved;
        UINT Flags;
    };
    OPEN_ADAPTER_COMPLETE_HANDLER OpenAdapterCompleteHandler;
    CLOSE_ADAPTER_COMPLETE_HANDLER CloseAdapterCompleteHandler;
    union {
        SEND_COMPLETE_HANDLER SendCompleteHandler;
        PVOID WanSendCompleteHandler;
    };
    union {
        TRANSFER_DATA_COMPLETE_HANDLER TransferDataCompleteHandler;
        PVOID WanTransferDataCompleteHandler;
    };
    RESET_COMPLETE_HANDLER ResetCompleteHandler;
    REQUEST_COMPLETE_HANDLER RequestCompleteHandler;
    union {
        RECEIVE_HANDLER ReceiveHandler;
        PVOID WanReceiveHandler;
    };
    RECEIVE_COMPLETE_HANDLER ReceiveCompleteHandler;
    STATUS_HANDLER StatusHandler;
    STATUS_COMPLETE_HANDLER StatusCompleteHandler;
    NDIS_STRING Name;
    PVOID ReceivePacketHandler;
    BIND_HANDLER BindAdapterHandler;
    UNBIND_HANDLER UnbindAdapterHandler;
    PNP_EVENT_HANDLER PnPEventHandler;
    UNLOAD_PROTOCOL_HANDLER UnloadHandler;
    PVOID ReservedHandlers[4];
    PVOID CoSendCompleteHandler;
    PVOID CoStatusHandler;
    PVOID CoReceivePacketHandler;
    PVOID CoAfRegisterNotifyHandler;
} NDIS_PROTOCOL_CHARACTERISTICS, *PNDIS_PROTOCOL_CHARACTERISTICS;

NDIS_STATUS NTAPI NdisRegisterProtocol(PNDIS_STATUS Status, PNDIS_HANDLE ProtocolHandle,
                                       PNDIS_PROTOCOL_CHARACTERISTICS Characteristics,
                                       UINT CharacteristicsLength);
VOID NTAPI NdisDeregisterProtocol(PNDIS_STATUS Status, NDIS_HANDLE ProtocolHandle);
VOID NTAPI NdisOpenAdapter(PNDIS_STATUS Status, PNDIS_STATUS OpenErrorStatus,
                           PNDIS_HANDLE BindingHandle, PUINT SelectedMediumIndex,
                           PNDIS_MEDIUM MediumArray, UINT MediumArraySize,
                           NDIS_HANDLE ProtocolHandle, NDIS_HANDLE ProtocolBindingContext,
                           PNDIS_STRING AdapterName, UINT OpenOptions,
                           PSTRING AddressingInformation);
VOID NTAPI NdisCloseAdapter(PNDIS_STATUS Status, NDIS_HANDLE BindingHandle);
VOID NTAPI NdisReEnumerateProtocolBindings(NDIS_HANDLE ProtocolHandle);
VOID NTAPI NdisCompleteBindAdapter(NDIS_HANDLE BindAdapterContext, NDIS_STATUS Status, NDIS_STATUS OpenStatus);
NDIS_STATUS NTAPI NdisRequest(PNDIS_STATUS Status, NDIS_HANDLE BindingHandle, PNDIS_REQUEST Request);
NDIS_STATUS NTAPI NdisAllocatePacketPool(PNDIS_STATUS Status, PNDIS_HANDLE PoolHandle, UINT NumberOfDescriptors, UINT ProtocolReservedLength);
NDIS_STATUS NTAPI NdisAllocateBufferPool(PNDIS_STATUS Status, PNDIS_HANDLE PoolHandle, UINT NumberOfDescriptors);
NDIS_STATUS NTAPI NdisAllocatePacket(PNDIS_STATUS Status, PNDIS_PACKET *Packet, NDIS_HANDLE PoolHandle);
NDIS_STATUS NTAPI NdisAllocateBuffer(PNDIS_STATUS Status, PNDIS_BUFFER *Buffer, NDIS_HANDLE PoolHandle, PVOID VirtualAddress, UINT Length);
VOID NTAPI NdisChainBufferAtBack(PNDIS_PACKET Packet, PNDIS_BUFFER Buffer);
NDIS_STATUS NTAPI NdisSend(PNDIS_STATUS Status, NDIS_HANDLE BindingHandle, PNDIS_PACKET Packet);
VOID NTAPI NdisGetFirstBufferFromPacket(PNDIS_PACKET Packet, PNDIS_BUFFER *FirstBuffer, PVOID *FirstBufferVA, PUINT FirstBufferLength, PUINT TotalBufferLength);
VOID NTAPI NdisFreeBuffer(PNDIS_BUFFER Buffer);
VOID NTAPI NdisFreePacket(PNDIS_PACKET Packet);

#endif
