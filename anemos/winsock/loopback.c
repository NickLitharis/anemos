#include "loopback.h"

#include <string.h>

static struct AnemLoopbackEndpoint *find_endpoint(AnemLoopback *lb, int id)
{
    if (id <= 0) {
        return NULL;
    }
    for (unsigned i = 0; i < ANEM_LOOPBACK_MAX; i++) {
        if (lb->ep[i].id == id) {
            return &lb->ep[i];
        }
    }
    return NULL;
}

static struct AnemLoopbackEndpoint *alloc_endpoint(AnemLoopback *lb, int id,
                                                   AnemSockType type)
{
    for (unsigned i = 0; i < ANEM_LOOPBACK_MAX; i++) {
        if (lb->ep[i].id == 0) {
            memset(&lb->ep[i], 0, sizeof lb->ep[i]);
            lb->ep[i].id = id;
            lb->ep[i].type = type;
            return &lb->ep[i];
        }
    }
    return NULL;
}

static struct AnemLoopbackEndpoint *find_port(AnemLoopback *lb,
                                              AnemSockType type,
                                              uint16_t port)
{
    for (unsigned i = 0; i < ANEM_LOOPBACK_MAX; i++) {
        struct AnemLoopbackEndpoint *ep = &lb->ep[i];

        if (ep->id != 0 && ep->type == type && ep->port == port) {
            return ep;
        }
    }
    return NULL;
}

static void handle_request(AnemLoopback *lb, const AnemSockRequest *request)
{
    struct AnemLoopbackEndpoint *ep = find_endpoint(lb, request->socket);
    struct AnemLoopbackEndpoint *peer;

    switch (request->kind) {
    case ANEM_REQUEST_OPEN:
        (void)alloc_endpoint(lb, request->socket,
                             (AnemSockType)request->value);
        break;
    case ANEM_REQUEST_BIND:
        if (ep != NULL) {
            ep->addr = request->addr;
            ep->port = request->port;
        }
        break;
    case ANEM_REQUEST_LISTEN:
        if (ep != NULL) {
            ep->listening = 1;
        }
        break;
    case ANEM_REQUEST_CONNECT: {
        struct AnemLoopbackEndpoint *dst;

        if (ep == NULL) {
            break;
        }
        dst = find_port(lb, ep->type, request->port);
        if (ep->type == ANEM_SOCK_DGRAM) {
            if (dst != NULL && dst->id != ep->id) {
                ep->peer = dst->id;
                dst->peer = ep->id;
            } else {
                (void)anem_socket_event_refused(ep->id);
            }
            break;
        }
        if (dst == NULL || dst->listening == 0) {
            (void)anem_socket_event_refused(ep->id);
            break;
        }
        if (dst->pending_count >= ANEM_LOOPBACK_PENDING) {
            (void)anem_socket_event_reset(ep->id);
            break;
        }
        dst->pending[dst->pending_count++] = ep->id;
        (void)anem_socket_event_incoming(dst->id);
        (void)anem_socket_event_connected(ep->id);
        break;
    }
    case ANEM_REQUEST_ACCEPT: {
        struct AnemLoopbackEndpoint *listener =
            find_endpoint(lb, (int)request->value);
        struct AnemLoopbackEndpoint *accepted =
            alloc_endpoint(lb, request->socket, ANEM_SOCK_STREAM);

        if (listener != NULL && accepted != NULL
            && listener->pending_count > 0) {
            int client_id = listener->pending[0];

            for (unsigned i = 1; i < listener->pending_count; i++) {
                listener->pending[i - 1] = listener->pending[i];
            }
            listener->pending_count--;
            accepted->type = listener->type;
            accepted->peer = client_id;
            peer = find_endpoint(lb, client_id);
            if (peer != NULL) {
                peer->peer = accepted->id;
            }
        }
        break;
    }
    case ANEM_REQUEST_SEND:
        peer = find_endpoint(lb, ep == NULL ? 0 : ep->peer);
        if (peer != NULL && request->data != NULL && request->value > 0u) {
            unsigned len = request->value;

#ifndef ANEM_LOOPBACK_NEGATIVE
            if (peer->rx_len + len <= ANEM_LOOPBACK_RX) {
#else
            if (len > 0u && peer->rx_len + len - 1u <= ANEM_LOOPBACK_RX) {
                len--;                   /* deliberate gate control: drop a byte */
#endif
                memcpy(peer->rx + peer->rx_len, request->data, len);
                peer->rx_len += len;
                (void)anem_socket_event_readable(peer->id, len);
            } else {
                (void)anem_socket_event_reset(peer->id);
            }
        }
        if (ep != NULL) {
            (void)anem_socket_event_writable(ep->id);
        }
        break;
    case ANEM_REQUEST_SHUTDOWN:
        peer = find_endpoint(lb, ep == NULL ? 0 : ep->peer);
        if (peer != NULL
            && (request->value == ANEM_SD_SEND
                || request->value == ANEM_SD_BOTH)) {
            (void)anem_socket_event_eof(peer->id);
        }
        break;
    case ANEM_REQUEST_CLOSE:
        if (ep != NULL) {
            peer = find_endpoint(lb, ep->peer);
            if (peer != NULL) {
                peer->peer = 0;
                (void)anem_socket_event_eof(peer->id);
            }
            memset(ep, 0, sizeof *ep);
        }
        break;
    case ANEM_REQUEST_REFUSE: {
        struct AnemLoopbackEndpoint *listener =
            find_endpoint(lb, request->socket);

        if (listener != NULL && listener->pending_count > 0) {
            int client_id = listener->pending[listener->pending_count - 1];

            listener->pending_count--;
            (void)anem_socket_event_reset(client_id);
        }
        break;
    }
    }
}

void anem_loopback_init(AnemLoopback *lb)
{
    if (lb != NULL) {
        memset(lb, 0, sizeof *lb);
    }
}

void anem_loopback_pump(AnemLoopback *lb)
{
    AnemSockRequest request;

    if (lb == NULL) {
        return;
    }
    while (anem_socket_take_request(&request) != 0) {
        handle_request(lb, &request);
    }
}

int anem_loopback_send(AnemLoopback *lb, int id, const void *data,
                       unsigned length)
{
    int rc = anem_socket_send(id, data, length);

    if (rc == 0) {
        anem_loopback_pump(lb);
    }
    return rc;
}

int anem_loopback_recv(AnemLoopback *lb, int id, void *buffer, unsigned cap,
                       unsigned *received)
{
    struct AnemLoopbackEndpoint *ep = find_endpoint(lb, id);
    unsigned count = 0;
    int rc = anem_socket_recv(id, cap, &count);

    if (rc == 0 && count > 0u && ep != NULL && buffer != NULL) {
        unsigned take = (count < ep->rx_len) ? count : ep->rx_len;

        memcpy(buffer, ep->rx, take);
        memmove(ep->rx, ep->rx + take, ep->rx_len - take);
        ep->rx_len -= take;
        count = take;
    }
    if (received != NULL) {
        *received = count;
    }
    anem_loopback_pump(lb);
    return rc;
}
