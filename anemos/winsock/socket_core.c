#include "socket_core.h"

#include <string.h>

typedef struct SocketSlot {
    int id;
    AnemSockType type;
    AnemSockState state;
    uint32_t addr;
    uint16_t port;
    unsigned backlog;
    unsigned pending;
    int shut_read;
    int shut_write;
} SocketSlot;

static SocketSlot g_sockets[ANEM_SOCKET_MAX];
static int g_next_id = 1;
static unsigned g_next_ephemeral = 49152u;

static AnemSockRequest g_requests[256];
static unsigned g_request_head;
static unsigned g_request_tail;
static unsigned g_request_count;
static int g_request_overflow;

static int port_in_use(uint16_t port);

static int fail(int error)
{
    return -error;
}

static SocketSlot *slot_of(int id)
{
    for (unsigned i = 0; i < ANEM_SOCKET_MAX; i++) {
        if (g_sockets[i].id == id && g_sockets[i].state != ANEM_STATE_CLOSED) {
            return &g_sockets[i];
        }
    }
    return NULL;
}

static SocketSlot *free_slot(void)
{
    for (unsigned i = 0; i < ANEM_SOCKET_MAX; i++) {
        if (g_sockets[i].state == ANEM_STATE_CLOSED && g_sockets[i].id == 0) {
            return &g_sockets[i];
        }
    }
    return NULL;
}

static void emit(AnemSockRequestKind kind, int id, uint32_t addr, uint16_t port,
                 unsigned value)
{
    if (g_request_count == sizeof g_requests / sizeof g_requests[0]) {
        g_request_overflow = 1;
        return;
    }
    AnemSockRequest *request = &g_requests[g_request_tail];
    request->kind = kind;
    request->socket = id;
    request->addr = addr;
    request->port = port;
    request->value = value;
    g_request_tail = (g_request_tail + 1u) % (unsigned)(sizeof g_requests / sizeof g_requests[0]);
    g_request_count++;
}

/* Next ephemeral port that is not already bound. Returns 0 when the range is
 * exhausted; callers turn that into WSAEADDRNOTAVAIL. */
static int take_ephemeral(uint16_t *out)
{
    for (unsigned tries = 0; tries < 16384u; tries++) {
        uint16_t port = (uint16_t)g_next_ephemeral;

        g_next_ephemeral = (g_next_ephemeral >= 65535u) ? 49152u : g_next_ephemeral + 1u;
        if (!port_in_use(port)) {
            *out = port;
            return 1;
        }
    }
    return 0;
}

static int port_in_use(uint16_t port)
{
    if (port == 0u) {
        return 0;
    }
    for (unsigned i = 0; i < ANEM_SOCKET_MAX; i++) {
        const SocketSlot *slot = &g_sockets[i];
        if (slot->state == ANEM_STATE_CLOSED || slot->state == ANEM_STATE_CREATED) {
            continue;
        }
        if (slot->port == port) {
            return 1;
        }
    }
    return 0;
}

void anem_socket_reset(void)
{
    memset(g_sockets, 0, sizeof g_sockets);
    g_next_id = 1;
    g_next_ephemeral = 49152u;
    g_request_head = 0;
    g_request_tail = 0;
    g_request_count = 0;
    g_request_overflow = 0;
}

int anem_socket_socket(AnemSockType type)
{
    SocketSlot *slot;
    int id;

    if (type != ANEM_SOCK_STREAM && type != ANEM_SOCK_DGRAM) {
        return fail(ANEM_WSAEINVAL);
    }
    slot = free_slot();
    if (slot == NULL) {
        return fail(ANEM_WSAENOBUFS);
    }
    id = g_next_id++;
    slot->id = id;
    slot->type = type;
    slot->state = ANEM_STATE_CREATED;
    slot->addr = 0;
    slot->port = 0;
    slot->backlog = 0;
    slot->pending = 0;
    slot->shut_read = 0;
    slot->shut_write = 0;
    emit(ANEM_REQUEST_OPEN, id, 0, 0, (unsigned)type);
    return id;
}

int anem_socket_bind(int id, uint32_t addr_net, uint16_t port_host)
{
    SocketSlot *slot = slot_of(id);

    if (slot == NULL) {
        return fail(ANEM_WSAENOTSOCK);
    }
    if (slot->state != ANEM_STATE_CREATED) {
        return fail(ANEM_WSAEINVAL);
    }
    if (addr_net == UINT32_C(0xffffffff)) {
        return fail(ANEM_WSAEADDRNOTAVAIL);   /* the broadcast address */
    }
    if (port_host == 0u) {
        if (!take_ephemeral(&port_host)) {
            return fail(ANEM_WSAEADDRNOTAVAIL);
        }
    } else if (port_in_use(port_host)) {
        return fail(ANEM_WSAEADDRINUSE);
    }
    slot->addr = addr_net;
    slot->port = port_host;
    slot->state = ANEM_STATE_BOUND;
    emit(ANEM_REQUEST_BIND, id, addr_net, port_host, 0);
    return 0;
}

int anem_socket_listen(int id, unsigned backlog)
{
    SocketSlot *slot = slot_of(id);

    if (slot == NULL) {
        return fail(ANEM_WSAENOTSOCK);
    }
    if (slot->type != ANEM_SOCK_STREAM) {
        return fail(ANEM_WSAEINVAL);
    }
    if (slot->state != ANEM_STATE_BOUND && slot->state != ANEM_STATE_LISTENING) {
        return fail(ANEM_WSAEINVAL);
    }
    /* Winsock leaves a zero backlog to the provider; one is the smallest
     * useful value and keeps the trace deterministic. Listening again just
     * updates the backlog, as it does on Windows. */
    slot->backlog = (backlog == 0u) ? 1u : backlog;
    slot->state = ANEM_STATE_LISTENING;
    emit(ANEM_REQUEST_LISTEN, id, slot->addr, slot->port, slot->backlog);
    return 0;
}

int anem_socket_connect(int id, uint32_t addr_net, uint16_t port_host)
{
    SocketSlot *slot = slot_of(id);

    if (slot == NULL) {
        return fail(ANEM_WSAENOTSOCK);
    }
    if (slot->state == ANEM_STATE_CONNECTING) {
        return fail(ANEM_WSAEALREADY);
    }
    if (slot->state == ANEM_STATE_CONNECTED) {
        return fail(ANEM_WSAEISCONN);
    }
    if (slot->state == ANEM_STATE_LISTENING) {
        return fail(ANEM_WSAEINVAL);
    }
    if (slot->state == ANEM_STATE_CREATED) {
        /* Winsock binds an unbound socket implicitly on connect. */
        uint16_t local = 0;

        if (!take_ephemeral(&local)) {
            return fail(ANEM_WSAEADDRNOTAVAIL);
        }
        slot->addr = 0;
        slot->port = local;
        slot->state = ANEM_STATE_BOUND;
        emit(ANEM_REQUEST_BIND, id, slot->addr, slot->port, 0);
    }
    /* A datagram connect only records the default peer: no handshake, so it
     * is complete immediately. Stream sockets wait for the transport event. */
    slot->state = (slot->type == ANEM_SOCK_DGRAM) ? ANEM_STATE_CONNECTED
                                                  : ANEM_STATE_CONNECTING;
    emit(ANEM_REQUEST_CONNECT, id, addr_net, port_host, 0);
    return 0;
}

int anem_socket_accept(int id)
{
    SocketSlot *listener = slot_of(id);
    SocketSlot *slot;
    int new_id;

    if (listener == NULL) {
        return fail(ANEM_WSAENOTSOCK);
    }
    if (listener->state != ANEM_STATE_LISTENING) {
        return fail(ANEM_WSAEINVAL);
    }
    if (listener->pending == 0u) {
        return fail(ANEM_WSAEWOULDBLOCK);
    }
    slot = free_slot();
    if (slot == NULL) {
        return fail(ANEM_WSAENOBUFS);
    }
#ifndef ANEM_SOCKET_NEGATIVE
    listener->pending--;
#endif
    new_id = g_next_id++;
    slot->id = new_id;
    slot->type = listener->type;
    slot->state = ANEM_STATE_CONNECTED;
    slot->addr = 0;
    slot->port = 0;
    slot->backlog = 0;
    slot->pending = 0;
    slot->shut_read = 0;
    slot->shut_write = 0;
    return new_id;
}

int anem_socket_shutdown(int id, unsigned how)
{
    SocketSlot *slot = slot_of(id);

    if (slot == NULL) {
        return fail(ANEM_WSAENOTSOCK);
    }
    if (slot->state != ANEM_STATE_CONNECTED) {
        return fail(ANEM_WSAENOTCONN);
    }
    if (how != ANEM_SD_RECEIVE && how != ANEM_SD_SEND && how != ANEM_SD_BOTH) {
        return fail(ANEM_WSAEINVAL);
    }
    if (how == ANEM_SD_RECEIVE || how == ANEM_SD_BOTH) {
        slot->shut_read = 1;
    }
    if (how == ANEM_SD_SEND || how == ANEM_SD_BOTH) {
        slot->shut_write = 1;
    }
    emit(ANEM_REQUEST_SHUTDOWN, id, 0, 0, how);
    return 0;
}

int anem_socket_close(int id)
{
    SocketSlot *slot = slot_of(id);

    if (slot == NULL) {
        return fail(ANEM_WSAENOTSOCK);
    }
    emit(ANEM_REQUEST_CLOSE, id, 0, 0, 0);
    memset(slot, 0, sizeof *slot);
    return 0;
}

AnemSockState anem_socket_state(int id)
{
    const SocketSlot *slot = slot_of(id);

    return slot == NULL ? ANEM_STATE_CLOSED : slot->state;
}

const char *anem_socket_state_name(AnemSockState state)
{
    switch (state) {
    case ANEM_STATE_CLOSED: return "closed";
    case ANEM_STATE_CREATED: return "created";
    case ANEM_STATE_BOUND: return "bound";
    case ANEM_STATE_LISTENING: return "listening";
    case ANEM_STATE_CONNECTING: return "connecting";
    case ANEM_STATE_CONNECTED: return "connected";
    }
    return "unknown";
}

void anem_socket_event_connected(int id)
{
    SocketSlot *slot = slot_of(id);

    if (slot != NULL && slot->state == ANEM_STATE_CONNECTING) {
        slot->state = ANEM_STATE_CONNECTED;
    }
}

void anem_socket_event_refused(int id)
{
    SocketSlot *slot = slot_of(id);

    if (slot != NULL && slot->state == ANEM_STATE_CONNECTING) {
        slot->state = ANEM_STATE_BOUND;
    }
}

void anem_socket_event_incoming(int id)
{
    SocketSlot *slot = slot_of(id);

    if (slot == NULL || slot->state != ANEM_STATE_LISTENING) {
        return;
    }
    if (slot->pending < slot->backlog) {
        slot->pending++;
    } else {
        emit(ANEM_REQUEST_REFUSE, id, 0, 0, 0);
    }
}

int anem_socket_request_overflow(void)
{
    return g_request_overflow;
}

int anem_socket_take_request(AnemSockRequest *out)
{
    if (out == NULL || g_request_count == 0u) {
        return 0;
    }
    *out = g_requests[g_request_head];
    g_request_head = (g_request_head + 1u)
                     % (unsigned)(sizeof g_requests / sizeof g_requests[0]);
    g_request_count--;
    return 1;
}
