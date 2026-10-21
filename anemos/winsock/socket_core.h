/* Socket core: the connection lifecycle of a Winsock socket, with no socket
 * underneath it. The core decides state and emits requests (open, bind,
 * listen, connect, shutdown, close, refuse) to a transport the caller drains.
 * Completed-transport events are injected back in. That makes every transition
 * a deterministic function of the operation trace — no kernel, no ports, no
 * timing — which is what the differential model tests.
 *
 * Winsock error numbers are the real published values, so the eventual XP
 * conformance probe compares numbers directly.
 */
#ifndef ANEM_SOCKET_CORE_H
#define ANEM_SOCKET_CORE_H

#include <stdint.h>

#include "winsock.h"

#define ANEM_WSAEINVAL 10022
#define ANEM_WSAEWOULDBLOCK 10035
#define ANEM_WSAEALREADY 10037
#define ANEM_WSAENOTSOCK 10038
#define ANEM_WSAEPROTOTYPE 10041
#define ANEM_WSAEOPNOTSUPP 10045
#define ANEM_WSAEAFNOSUPPORT 10047
#define ANEM_WSAEADDRINUSE 10048
#define ANEM_WSAEADDRNOTAVAIL 10049
#define ANEM_WSAENOBUFS 10055
#define ANEM_WSAEISCONN 10056
#define ANEM_WSAENOTCONN 10057
#define ANEM_WSAECONNREFUSED 10061

#define ANEM_SOCKET_MAX 64

#define ANEM_SD_RECEIVE 0
#define ANEM_SD_SEND 1
#define ANEM_SD_BOTH 2

typedef enum AnemSockType {
    ANEM_SOCK_STREAM = 1,
    ANEM_SOCK_DGRAM = 2
} AnemSockType;

typedef enum AnemSockState {
    ANEM_STATE_CLOSED = 0,
    ANEM_STATE_CREATED,
    ANEM_STATE_BOUND,
    ANEM_STATE_LISTENING,
    ANEM_STATE_CONNECTING,
    ANEM_STATE_CONNECTED
} AnemSockState;

typedef enum AnemSockRequestKind {
    ANEM_REQUEST_OPEN = 1,
    ANEM_REQUEST_BIND,
    ANEM_REQUEST_LISTEN,
    ANEM_REQUEST_CONNECT,
    ANEM_REQUEST_SHUTDOWN,
    ANEM_REQUEST_CLOSE,
    ANEM_REQUEST_REFUSE
} AnemSockRequestKind;

typedef struct AnemSockRequest {
    AnemSockRequestKind kind;
    int socket;
    uint32_t addr;
    uint16_t port;
    unsigned value;
} AnemSockRequest;

/* All functions return 0 on success and a negated WSA error on failure, except
 * the ones documented to return an identifier. */
void anem_socket_reset(void);
int anem_socket_socket(AnemSockType type);
int anem_socket_bind(int id, uint32_t addr_net, uint16_t port_host);
int anem_socket_listen(int id, unsigned backlog);
int anem_socket_connect(int id, uint32_t addr_net, uint16_t port_host);
int anem_socket_accept(int id);
int anem_socket_shutdown(int id, unsigned how);
int anem_socket_close(int id);
AnemSockState anem_socket_state(int id);
const char *anem_socket_state_name(AnemSockState state);

/* Transport events. Unknown or out-of-state events are ignored. */
void anem_socket_event_connected(int id);
void anem_socket_event_refused(int id);
void anem_socket_event_incoming(int id);

/* Drain the request queue, one request per call. Returns 1 when a request was
 * written, 0 when the queue is empty. */
int anem_socket_take_request(AnemSockRequest *out);

/* Set when a request had to be dropped because the queue was full. A real
 * transport must treat this as an internal error; the trace gate fails if it
 * ever appears. */
int anem_socket_request_overflow(void);

#endif
