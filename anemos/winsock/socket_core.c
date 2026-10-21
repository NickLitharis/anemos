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
    unsigned rx_available;
    int writable;
    int eof;
    int reset;
    int error;
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

static int port_in_use(uint16_t port, AnemSockType type);

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

static AnemSockRequest *emit(AnemSockRequestKind kind, int id, uint32_t addr,
                             uint16_t port, unsigned value)
{
    if (g_request_count == sizeof g_requests / sizeof g_requests[0]) {
        g_request_overflow = 1;
        return NULL;
    }
    AnemSockRequest *request = &g_requests[g_request_tail];
    request->kind = kind;
    request->socket = id;
    request->addr = addr;
    request->port = port;
    request->value = value;
    request->data = NULL;
    g_request_tail = (g_request_tail + 1u) % (unsigned)(sizeof g_requests / sizeof g_requests[0]);
    g_request_count++;
    return request;
}

/* Next ephemeral port that is not already bound. Returns 0 when the range is
 * exhausted; callers turn that into WSAEADDRNOTAVAIL. */
static int take_ephemeral(uint16_t *out, AnemSockType type)
{
    for (unsigned tries = 0; tries < 16384u; tries++) {
        uint16_t port = (uint16_t)g_next_ephemeral;

        g_next_ephemeral = (g_next_ephemeral >= 65535u) ? 49152u : g_next_ephemeral + 1u;
        if (!port_in_use(port, type)) {
            *out = port;
            return 1;
        }
    }
    return 0;
}

/* Winsock namespaces TCP and UDP ports separately. Address specificity is not
 * modelled yet: a wildcard bind conflicts with a specific one on the same
 * protocol, which is the default (no SO_REUSEADDR) behaviour. */
static int port_in_use(uint16_t port, AnemSockType type)
{
    if (port == 0u) {
        return 0;
    }
    for (unsigned i = 0; i < ANEM_SOCKET_MAX; i++) {
        const SocketSlot *slot = &g_sockets[i];
        if (slot->state == ANEM_STATE_CLOSED || slot->state == ANEM_STATE_CREATED) {
            continue;
        }
        if (slot->port == port && slot->type == type) {
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
    slot->rx_available = 0;
    slot->writable = 0;
    slot->eof = 0;
    slot->reset = 0;
    slot->error = 0;
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
        if (!take_ephemeral(&port_host, slot->type)) {
            return fail(ANEM_WSAEADDRNOTAVAIL);
        }
    } else if (port_in_use(port_host, slot->type)) {
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
        return fail(ANEM_WSAEOPNOTSUPP);
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
    slot->error = 0;
    if (slot->state == ANEM_STATE_CREATED) {
        /* Winsock binds an unbound socket implicitly on connect. */
        uint16_t local = 0;

        if (!take_ephemeral(&local, slot->type)) {
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
    if (slot->state == ANEM_STATE_CONNECTED) {
        slot->writable = 1;
    }
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
    slot->rx_available = 0;
    slot->writable = 1;                  /* an accepted socket can send */
    slot->eof = 0;
    slot->reset = 0;
    slot->error = 0;
    slot->shut_read = 0;
    slot->shut_write = 0;
    emit(ANEM_REQUEST_ACCEPT, new_id, 0, 0, (unsigned)id);
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
    if (((how == ANEM_SD_RECEIVE || how == ANEM_SD_BOTH) && slot->shut_read != 0)
        || ((how == ANEM_SD_SEND || how == ANEM_SD_BOTH) && slot->shut_write != 0)) {
        return fail(ANEM_WSAENOTCONN);
    }
    if (how == ANEM_SD_RECEIVE || how == ANEM_SD_BOTH) {
        slot->shut_read = 1;
        slot->rx_available = 0;
    }
    if (how == ANEM_SD_SEND || how == ANEM_SD_BOTH) {
        slot->shut_write = 1;
        slot->writable = 0;
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
        slot->writable = 1;
        slot->error = 0;
    }
}

void anem_socket_event_refused(int id)
{
    SocketSlot *slot = slot_of(id);

    if (slot != NULL && slot->state == ANEM_STATE_CONNECTING) {
        slot->state = ANEM_STATE_BOUND;
        slot->error = ANEM_WSAECONNREFUSED;
        /* XP does not mark a refused connect write-ready; SO_ERROR is the
         * only report (P4.6 certificate). */
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

int anem_socket_send(int id, const void *data, unsigned length)
{
    SocketSlot *slot = slot_of(id);

    if (slot == NULL) {
        return fail(ANEM_WSAENOTSOCK);
    }
    if (slot->state != ANEM_STATE_CONNECTED) {
        return fail(ANEM_WSAENOTCONN);
    }
    if (slot->reset != 0) {
        return fail(ANEM_WSAECONNRESET);
    }
    if (slot->shut_write != 0) {
        return fail(ANEM_WSAESHUTDOWN);
    }
    if (length == 0u) {
        return 0;                        /* nothing to hand to the transport */
    }
    if (data == NULL) {
        return fail(ANEM_WSAEINVAL);
    }
    if (slot->writable == 0) {
        return fail(ANEM_WSAEWOULDBLOCK);
    }
    /* One accepted send clears writability; the transport's writable event
     * restores it when the queue drains. */
    slot->writable = 0;
    {
        AnemSockRequest *request = emit(ANEM_REQUEST_SEND, id, 0, 0, length);

        if (request != NULL) {
            request->data = data;
        }
    }
    return 0;
}

int anem_socket_recv(int id, unsigned length, unsigned *received)
{
    SocketSlot *slot = slot_of(id);
    unsigned take;

    if (received == NULL) {
        return fail(ANEM_WSAEINVAL);
    }
    *received = 0;
    if (slot == NULL) {
        return fail(ANEM_WSAENOTSOCK);
    }
    if (slot->state != ANEM_STATE_CONNECTED) {
        return fail(ANEM_WSAENOTCONN);
    }
    if (slot->reset != 0) {
        return fail(ANEM_WSAECONNRESET);
    }
    if (slot->shut_read != 0) {
        return fail(ANEM_WSAESHUTDOWN);
    }
    if (length == 0u) {
        return 0;
    }
    take = (length < slot->rx_available) ? length : slot->rx_available;
    if (take > 0u) {
        slot->rx_available -= take;
        *received = take;
        return 0;
    }
    if (slot->eof != 0) {
        return 0;                        /* graceful close: zero bytes */
    }
    return fail(ANEM_WSAEWOULDBLOCK);
}

int anem_socket_select(const int *read_ids, unsigned read_count,
                       const int *write_ids, unsigned write_count,
                       int *read_ready, int *write_ready)
{
    int ready = 0;

    if ((read_count > 0u && (read_ids == NULL || read_ready == NULL))
        || (write_count > 0u && (write_ids == NULL || write_ready == NULL))) {
        return fail(ANEM_WSAEINVAL);
    }
    for (unsigned i = 0; i < read_count; i++) {
        if (slot_of(read_ids[i]) == NULL) {
            return fail(ANEM_WSAENOTSOCK);
        }
    }
    for (unsigned i = 0; i < write_count; i++) {
        if (slot_of(write_ids[i]) == NULL) {
            return fail(ANEM_WSAENOTSOCK);
        }
    }
    for (unsigned i = 0; i < read_count; i++) {
        const SocketSlot *slot = slot_of(read_ids[i]);
        /* Readiness means "will not block": a queued connection, buffered
         * bytes, EOF, a reset, or a shutdown that makes recv fail at once. */
        read_ready[i] = ((slot->state == ANEM_STATE_LISTENING && slot->pending > 0u)
                         || slot->rx_available > 0u || slot->eof != 0
                         || slot->reset != 0 || slot->shut_read != 0) ? 1 : 0;
        ready += read_ready[i];
    }
    for (unsigned i = 0; i < write_count; i++) {
        const SocketSlot *slot = slot_of(write_ids[i]);
        write_ready[i] = (slot->writable != 0 || slot->reset != 0
                          || slot->shut_write != 0) ? 1 : 0;
        ready += write_ready[i];
    }
    return ready;
}

void anem_socket_event_readable(int id, unsigned bytes)
{
    SocketSlot *slot = slot_of(id);

    if (slot == NULL || slot->state != ANEM_STATE_CONNECTED || slot->reset != 0
        || slot->eof != 0) {
        return;
    }
    if (slot->shut_read == 0) {
        if (slot->rx_available > UINT32_MAX - bytes) {
            slot->rx_available = UINT32_MAX;
        } else {
            slot->rx_available += bytes;
        }
    }
}

void anem_socket_event_writable(int id)
{
    SocketSlot *slot = slot_of(id);

    if (slot != NULL && slot->state == ANEM_STATE_CONNECTED
        && slot->shut_write == 0 && slot->reset == 0) {
        slot->writable = 1;
    }
}

void anem_socket_event_eof(int id)
{
    SocketSlot *slot = slot_of(id);

    if (slot != NULL && slot->state == ANEM_STATE_CONNECTED) {
        slot->eof = 1;
    }
}

void anem_socket_event_reset(int id)
{
    SocketSlot *slot = slot_of(id);

    if (slot == NULL) {
        return;
    }
    if (slot->state == ANEM_STATE_CONNECTED) {
        slot->reset = 1;
        slot->rx_available = 0;          /* a reset discards buffered bytes */
    } else if (slot->state == ANEM_STATE_CONNECTING) {
        slot->state = ANEM_STATE_BOUND;
        slot->error = ANEM_WSAECONNRESET;
    }
}

int anem_socket_error(int id)
{
    const SocketSlot *slot = slot_of(id);

    return slot == NULL ? fail(ANEM_WSAENOTSOCK) : slot->error;
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
