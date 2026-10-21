/* Loopback transport: a reference implementation of the seam below the
 * socket core. It consumes the core's request queue and injects the events a
 * real transport would, connecting sockets to each other in one process.
 *
 * This is what lets the user-space Winsock surface be exercised end to end
 * (connect, accept, send/recv bytes, EOF, refusal, backlog) without a kernel,
 * a NIC or a stack. The lwIP-vs-ReactOS decision only replaces this file's
 * bottom half, not the interface or the socket core above it.
 */
#ifndef ANEM_LOOPBACK_H
#define ANEM_LOOPBACK_H

#include <stdint.h>

#include "socket_core.h"

#define ANEM_LOOPBACK_MAX 64
#define ANEM_LOOPBACK_RX 4096
#define ANEM_LOOPBACK_PENDING 8

typedef struct AnemLoopback {
    struct AnemLoopbackEndpoint {
        int id;                 /* socket-core id, 0 when free */
        AnemSockType type;
        uint32_t addr;
        uint16_t port;
        int listening;
        int peer;               /* connected peer id, 0 when none */
        int pending[ANEM_LOOPBACK_PENDING];
        unsigned pending_count;
        uint8_t rx[ANEM_LOOPBACK_RX];
        unsigned rx_len;
    } ep[ANEM_LOOPBACK_MAX];
} AnemLoopback;

void anem_loopback_init(AnemLoopback *lb);

/* Drain every queued core request and perform it. */
void anem_loopback_pump(AnemLoopback *lb);

/* Convenience wrappers: send copies through the core and immediately pumps;
 * recv performs the core receive and then removes the same bytes from the
 * loopback queue. */
int anem_loopback_send(AnemLoopback *lb, int id, const void *data,
                       unsigned length);
int anem_loopback_recv(AnemLoopback *lb, int id, void *buffer, unsigned cap,
                       unsigned *received);

#endif
