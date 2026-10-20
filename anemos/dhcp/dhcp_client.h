/* DHCP client state machine — RFC 2131 §4.4, deterministic and host-testable.
 *
 * This unit owns no socket and no clock. The caller supplies the current time
 * in milliseconds, delivers received frames, and gets back at most one frame
 * to send. That makes the whole machine exercisable without a network and
 * without a VM.
 *
 * States are the RFC's client states minus INIT-REBOOT:
 *
 *   INIT -> SELECTING -> REQUESTING -> BOUND -> RENEWING -> REBINDING
 *              ^            |            |          |           |
 *              +------------+------------+----------+-----------+  (NAK/expiry)
 *
 * Determinism:
 *   - retransmission is exponential, 4 s doubling to a 64 s cap, with no
 *     jitter. RFC 2131 recommends randomised delays; a deterministic machine
 *     is required to diff it against a second implementation. The caller can
 *     add jitter on top; the state machine's timing is exact here.
 *   - transaction IDs come from a caller-seeded LCG. Seed it from entropy in
 *     production; tests seed it to reproduce a trace. The xid is stable across
 *     retransmissions of one transaction and changes at each restart.
 *
 * Time is milliseconds in a uint64_t; additions saturate at UINT64_MAX.
 *
 * No allocation, no globals, freestanding. See docs/P2-DHCP-CLIENT.md.
 */
#ifndef ANEMOS_DHCP_CLIENT_H
#define ANEMOS_DHCP_CLIENT_H

#include <stddef.h>
#include <stdint.h>

#include "dhcp.h"

#define ANEM_DHCP_RETRANSMIT_MS 4000u
#define ANEM_DHCP_RETRANSMIT_MAX_MS 64000u
#define ANEM_DHCP_RENEW_MIN_MS 60000u

typedef enum AnemDhcpState {
    ANEM_DHCP_STATE_INIT = 0,
    ANEM_DHCP_STATE_SELECTING = 1,
    ANEM_DHCP_STATE_REQUESTING = 2,
    ANEM_DHCP_STATE_BOUND = 3,
    ANEM_DHCP_STATE_RENEWING = 4,
    ANEM_DHCP_STATE_REBINDING = 5
} AnemDhcpState;

typedef struct AnemDhcpClient {
    AnemDhcpState state;
    uint8_t htype;
    uint8_t hlen;
    uint8_t chaddr[ANEM_DHCP_CHADDR_SIZE];
    uint32_t xid;
    uint32_t seed;        /* xid LCG state */
    uint32_t offered_ip;  /* yiaddr from OFFER */
    uint32_t ip;          /* leased address */
    uint32_t server_id;
    uint32_t lease_secs;
    uint32_t t1_secs;
    uint32_t t2_secs;
    uint64_t next_ms;     /* next retransmission or state deadline */
    uint64_t t1_at_ms;
    uint64_t t2_at_ms;
    uint64_t lease_at_ms;
    unsigned attempts;
} AnemDhcpClient;

/* Initialise in INIT. `seed` seeds the xid LCG; `chaddr` is 16 bytes (only
 * the first `hlen` are used and compared). hlen must be 1..16. */
AnemDhcpStatus anem_dhcp_client_init(AnemDhcpClient *cl, uint32_t seed,
                                     const uint8_t chaddr[ANEM_DHCP_CHADDR_SIZE],
                                     uint8_t hlen);

/* Leave INIT: emit a DISCOVER and enter SELECTING. BADSTATE if not in INIT. */
AnemDhcpStatus anem_dhcp_client_start(AnemDhcpClient *cl, uint64_t now_ms,
                                      void *out, size_t cap, size_t *out_len);

/* Deliver one received frame. A frame that is irrelevant (wrong xid, wrong
 * op, wrong chaddr, unknown type) is ignored: OK with *out_len == 0. A frame
 * that does not parse returns the codec's status. */
AnemDhcpStatus anem_dhcp_client_receive(AnemDhcpClient *cl, uint64_t now_ms,
                                        const void *wire, size_t wire_len,
                                        void *out, size_t cap, size_t *out_len);

/* Advance time: retransmit, renew, rebind, expire, or do nothing. On expiry
 * or NAK the client restarts with a DISCOVER. *out_len == 0 means "send
 * nothing". */
AnemDhcpStatus anem_dhcp_client_tick(AnemDhcpClient *cl, uint64_t now_ms,
                                     void *out, size_t cap, size_t *out_len);

#endif /* ANEMOS_DHCP_CLIENT_H */
