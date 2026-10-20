/* DHCP client state machine — see dhcp_client.h for the contract. */
#include "dhcp_client.h"

static void copy_bytes(void *dst, const void *src, size_t n)
{
    uint8_t *d = dst;
    const uint8_t *s = src;

    for (size_t i = 0; i < n; i++) {
        d[i] = s[i];
    }
}

static bool bytes_equal(const uint8_t *a, const uint8_t *b, size_t n)
{
    for (size_t i = 0; i < n; i++) {
        if (a[i] != b[i]) {
            return false;
        }
    }
    return true;
}

static uint64_t add_ms(uint64_t a, uint64_t b)
{
    return (a > UINT64_MAX - b) ? UINT64_MAX : a + b;
}

static uint64_t secs_to_ms(uint32_t secs)
{
    return (uint64_t)secs * 1000u;
}

/* Deterministic xid source. Seeded by the caller; documented as such. */
static uint32_t next_xid(AnemDhcpClient *cl)
{
    cl->seed = cl->seed * 1664525u + 1013904223u;
    return cl->seed;
}

static uint64_t backoff_ms(unsigned attempts)
{
#ifdef ANEM_DHCP_CLIENT_NEGATIVE
    /* Negative control: the wrong base interval. The scenario checks and the
     * trace differential must both reject this build. */
    (void)attempts;
    return 5000u;
#else
    if (attempts <= 1u) {
        return ANEM_DHCP_RETRANSMIT_MS;
    }
    if (attempts >= 5u) {
        return ANEM_DHCP_RETRANSMIT_MAX_MS;
    }
    return (uint64_t)ANEM_DHCP_RETRANSMIT_MS << (attempts - 1u);
#endif
}

/* Half the time to `deadline`, floored at one minute (RFC 2131 §4.4.5). */
static uint64_t renew_interval_ms(uint64_t deadline, uint64_t now)
{
    uint64_t half = (deadline > now) ? (deadline - now) / 2u : 0;

    return half < ANEM_DHCP_RENEW_MIN_MS ? ANEM_DHCP_RENEW_MIN_MS : half;
}

static AnemDhcpStatus emit_discover(AnemDhcpClient *cl, void *out, size_t cap,
                                    size_t *out_len)
{
    AnemDhcpHeader hdr;
    AnemDhcpWriter w;
    AnemDhcpStatus st;
    uint8_t cid[1u + ANEM_DHCP_CHADDR_SIZE];
    static const uint8_t params[3] = { 1, 3, 6 };

    st = anem_dhcp_header_init(&hdr);
    if (st != ANEM_DHCP_OK) {
        return st;
    }
    hdr.hlen = cl->hlen;
    hdr.xid = cl->xid;
    hdr.flags = ANEM_DHCP_FLAG_BROADCAST;
    copy_bytes(hdr.chaddr, cl->chaddr, ANEM_DHCP_CHADDR_SIZE);
    st = anem_dhcp_build_begin(&w, out, cap, &hdr);
    if (st != ANEM_DHCP_OK) {
        return st;
    }
    st = anem_dhcp_build_option_u8(&w, ANEM_DHCP_OPT_MESSAGE_TYPE,
                                   ANEM_DHCP_DISCOVER);
    if (st != ANEM_DHCP_OK) {
        return st;
    }
    st = anem_dhcp_build_option(&w, ANEM_DHCP_OPT_PARAM_LIST, params,
                                (uint8_t)sizeof params);
    if (st != ANEM_DHCP_OK) {
        return st;
    }
    cid[0] = 1; /* client identifier: hardware type 1 + address */
    copy_bytes(cid + 1u, cl->chaddr, cl->hlen);
    st = anem_dhcp_build_option(&w, ANEM_DHCP_OPT_CLIENT_ID, cid,
                                (uint8_t)(1u + (unsigned)cl->hlen));
    if (st != ANEM_DHCP_OK) {
        return st;
    }
    return anem_dhcp_build_finish(&w, out_len);
}

static AnemDhcpStatus emit_request(AnemDhcpClient *cl, bool selecting,
                                   bool broadcast, void *out, size_t cap,
                                   size_t *out_len)
{
    AnemDhcpHeader hdr;
    AnemDhcpWriter w;
    AnemDhcpStatus st;
    uint8_t cid[1u + ANEM_DHCP_CHADDR_SIZE];

    st = anem_dhcp_header_init(&hdr);
    if (st != ANEM_DHCP_OK) {
        return st;
    }
    hdr.hlen = cl->hlen;
    hdr.xid = cl->xid;
    hdr.flags = broadcast ? ANEM_DHCP_FLAG_BROADCAST : 0u;
    hdr.ciaddr = selecting ? 0u : cl->ip;
    copy_bytes(hdr.chaddr, cl->chaddr, ANEM_DHCP_CHADDR_SIZE);
    st = anem_dhcp_build_begin(&w, out, cap, &hdr);
    if (st != ANEM_DHCP_OK) {
        return st;
    }
    st = anem_dhcp_build_option_u8(&w, ANEM_DHCP_OPT_MESSAGE_TYPE,
                                   ANEM_DHCP_REQUEST);
    if (st != ANEM_DHCP_OK) {
        return st;
    }
    if (selecting) {
        st = anem_dhcp_build_option_u32(&w, ANEM_DHCP_OPT_REQUESTED_IP,
                                        cl->offered_ip);
        if (st != ANEM_DHCP_OK) {
            return st;
        }
        st = anem_dhcp_build_option_u32(&w, ANEM_DHCP_OPT_SERVER_ID,
                                        cl->server_id);
        if (st != ANEM_DHCP_OK) {
            return st;
        }
    }
    cid[0] = 1;
    copy_bytes(cid + 1u, cl->chaddr, cl->hlen);
    st = anem_dhcp_build_option(&w, ANEM_DHCP_OPT_CLIENT_ID, cid,
                                (uint8_t)(1u + (unsigned)cl->hlen));
    if (st != ANEM_DHCP_OK) {
        return st;
    }
    return anem_dhcp_build_finish(&w, out_len);
}

/* Read the lease and derive T1/T2. Returns false for an ACK without a usable
 * lease; such an ACK is ignored rather than half-applied. */
static bool apply_lease(AnemDhcpClient *cl, const AnemDhcpMessage *msg,
                        uint64_t now_ms)
{
    uint32_t lease;
    uint32_t t1;
    uint32_t t2;

    if (anem_dhcp_option_u32(msg, ANEM_DHCP_OPT_LEASE_TIME, &lease)
            != ANEM_DHCP_OK
        || lease == 0u) {
        return false;
    }
    if (anem_dhcp_option_u32(msg, ANEM_DHCP_OPT_T1, &t1) != ANEM_DHCP_OK
        || t1 == 0u || t1 >= lease) {
        t1 = lease / 2u;
    }
    if (anem_dhcp_option_u32(msg, ANEM_DHCP_OPT_T2, &t2) != ANEM_DHCP_OK
        || t2 <= t1 || t2 >= lease) {
        t2 = (uint32_t)(((uint64_t)lease * 7u) / 8u);
    }
    cl->lease_secs = lease;
    cl->t1_secs = t1;
    cl->t2_secs = t2;
    cl->t1_at_ms = add_ms(now_ms, secs_to_ms(t1));
    cl->t2_at_ms = add_ms(now_ms, secs_to_ms(t2));
    cl->lease_at_ms = add_ms(now_ms, secs_to_ms(lease));
    cl->next_ms = cl->t1_at_ms;
    cl->attempts = 0;
    return true;
}

static AnemDhcpStatus restart(AnemDhcpClient *cl, uint64_t now_ms, void *out,
                              size_t cap, size_t *out_len)
{
    AnemDhcpStatus st;

    cl->xid = next_xid(cl);
    st = emit_discover(cl, out, cap, out_len);
    if (st != ANEM_DHCP_OK) {
        cl->state = ANEM_DHCP_STATE_INIT;
        *out_len = 0;
        return st;
    }
    cl->state = ANEM_DHCP_STATE_SELECTING;
    cl->attempts = 1;
    cl->next_ms = add_ms(now_ms, ANEM_DHCP_RETRANSMIT_MS);
    cl->offered_ip = 0;
    cl->ip = 0;
    cl->server_id = 0;
    cl->lease_secs = 0;
    cl->t1_secs = 0;
    cl->t2_secs = 0;
    cl->t1_at_ms = 0;
    cl->t2_at_ms = 0;
    cl->lease_at_ms = 0;
    return ANEM_DHCP_OK;
}

AnemDhcpStatus anem_dhcp_client_init(AnemDhcpClient *cl, uint32_t seed,
                                     const uint8_t chaddr[ANEM_DHCP_CHADDR_SIZE],
                                     uint8_t hlen)
{
    if (cl == NULL || chaddr == NULL) {
        return ANEM_DHCP_BADARG;
    }
    if (hlen == 0u || hlen > ANEM_DHCP_CHADDR_SIZE) {
        return ANEM_DHCP_BADARG;
    }
    for (size_t i = 0; i < sizeof *cl; i++) {
        ((uint8_t *)cl)[i] = 0;
    }
    cl->state = ANEM_DHCP_STATE_INIT;
    cl->htype = ANEM_DHCP_HTYPE_ETHERNET;
    cl->hlen = hlen;
    copy_bytes(cl->chaddr, chaddr, ANEM_DHCP_CHADDR_SIZE);
    cl->seed = seed;
    return ANEM_DHCP_OK;
}

AnemDhcpStatus anem_dhcp_client_start(AnemDhcpClient *cl, uint64_t now_ms,
                                      void *out, size_t cap, size_t *out_len)
{
    if (cl == NULL || out == NULL || out_len == NULL) {
        return ANEM_DHCP_BADARG;
    }
    *out_len = 0;
    if (cl->state != ANEM_DHCP_STATE_INIT) {
        return ANEM_DHCP_BADSTATE;
    }
    return restart(cl, now_ms, out, cap, out_len);
}

AnemDhcpStatus anem_dhcp_client_receive(AnemDhcpClient *cl, uint64_t now_ms,
                                        const void *wire, size_t wire_len,
                                        void *out, size_t cap, size_t *out_len)
{
    AnemDhcpMessage msg;
    AnemDhcpStatus st;
    uint8_t type = 0;
    uint32_t server = 0;
    uint32_t yiaddr;

    if (cl == NULL || wire == NULL || out == NULL || out_len == NULL) {
        return ANEM_DHCP_BADARG;
    }
    *out_len = 0;
    st = anem_dhcp_parse(wire, wire_len, &msg);
    if (st != ANEM_DHCP_OK) {
        return st;
    }
    if (msg.header.op != ANEM_DHCP_BOOTREPLY || msg.header.xid != cl->xid
        || msg.header.hlen != cl->hlen
        || !bytes_equal(msg.header.chaddr, cl->chaddr, cl->hlen)) {
        return ANEM_DHCP_OK;
    }
    if (anem_dhcp_option_u8(&msg, ANEM_DHCP_OPT_MESSAGE_TYPE, &type)
            != ANEM_DHCP_OK
        || anem_dhcp_option_u32(&msg, ANEM_DHCP_OPT_SERVER_ID, &server)
               != ANEM_DHCP_OK) {
        return ANEM_DHCP_OK;
    }
    yiaddr = msg.header.yiaddr;

    if (cl->state == ANEM_DHCP_STATE_SELECTING
        && type == ANEM_DHCP_OFFER && yiaddr != 0u) {
        cl->offered_ip = yiaddr;
        cl->server_id = server;
        st = emit_request(cl, true, true, out, cap, out_len);
        if (st != ANEM_DHCP_OK) {
            return st;
        }
        cl->state = ANEM_DHCP_STATE_REQUESTING;
        cl->attempts = 1;
        cl->next_ms = add_ms(now_ms, ANEM_DHCP_RETRANSMIT_MS);
        return ANEM_DHCP_OK;
    }

    if (type == ANEM_DHCP_NAK
        && (cl->state == ANEM_DHCP_STATE_REQUESTING
            || cl->state == ANEM_DHCP_STATE_RENEWING
            || cl->state == ANEM_DHCP_STATE_REBINDING)) {
        return restart(cl, now_ms, out, cap, out_len);
    }

    if (type != ANEM_DHCP_ACK) {
        return ANEM_DHCP_OK;
    }

    if (cl->state == ANEM_DHCP_STATE_REQUESTING) {
        if (server != cl->server_id || yiaddr != cl->offered_ip
            || !apply_lease(cl, &msg, now_ms)) {
            return ANEM_DHCP_OK;
        }
        cl->ip = yiaddr;
        cl->state = ANEM_DHCP_STATE_BOUND;
        return ANEM_DHCP_OK;
    }
    if (cl->state == ANEM_DHCP_STATE_RENEWING) {
        if (server != cl->server_id || !apply_lease(cl, &msg, now_ms)) {
            return ANEM_DHCP_OK;
        }
        if (yiaddr != 0u) {
            cl->ip = yiaddr;
        }
        cl->state = ANEM_DHCP_STATE_BOUND;
        return ANEM_DHCP_OK;
    }
    if (cl->state == ANEM_DHCP_STATE_REBINDING) {
        if (!apply_lease(cl, &msg, now_ms)) {
            return ANEM_DHCP_OK;
        }
        if (yiaddr != 0u) {
            cl->ip = yiaddr;
        }
        cl->server_id = server;
        cl->state = ANEM_DHCP_STATE_BOUND;
        return ANEM_DHCP_OK;
    }
    return ANEM_DHCP_OK;
}

AnemDhcpStatus anem_dhcp_client_tick(AnemDhcpClient *cl, uint64_t now_ms,
                                     void *out, size_t cap, size_t *out_len)
{
    AnemDhcpStatus st;

    if (cl == NULL || out == NULL || out_len == NULL) {
        return ANEM_DHCP_BADARG;
    }
    *out_len = 0;

    switch (cl->state) {
    case ANEM_DHCP_STATE_INIT:
        return ANEM_DHCP_OK;
    case ANEM_DHCP_STATE_SELECTING:
        if (now_ms < cl->next_ms) {
            return ANEM_DHCP_OK;
        }
        st = emit_discover(cl, out, cap, out_len);
        if (st != ANEM_DHCP_OK) {
            return st;
        }
        cl->attempts++;
        cl->next_ms = add_ms(now_ms, backoff_ms(cl->attempts));
        return ANEM_DHCP_OK;
    case ANEM_DHCP_STATE_REQUESTING:
        if (now_ms < cl->next_ms) {
            return ANEM_DHCP_OK;
        }
        st = emit_request(cl, true, true, out, cap, out_len);
        if (st != ANEM_DHCP_OK) {
            return st;
        }
        cl->attempts++;
        cl->next_ms = add_ms(now_ms, backoff_ms(cl->attempts));
        return ANEM_DHCP_OK;
    case ANEM_DHCP_STATE_BOUND:
        if (now_ms < cl->t1_at_ms) {
            return ANEM_DHCP_OK;
        }
        st = emit_request(cl, false, false, out, cap, out_len);
        if (st != ANEM_DHCP_OK) {
            return st;
        }
        cl->state = ANEM_DHCP_STATE_RENEWING;
        cl->attempts = 1;
        cl->next_ms = add_ms(now_ms,
                             renew_interval_ms(cl->t2_at_ms, now_ms));
        return ANEM_DHCP_OK;
    case ANEM_DHCP_STATE_RENEWING:
        if (now_ms >= cl->lease_at_ms) {
            return restart(cl, now_ms, out, cap, out_len);
        }
        if (now_ms >= cl->t2_at_ms) {
            st = emit_request(cl, false, true, out, cap, out_len);
            if (st != ANEM_DHCP_OK) {
                return st;
            }
            cl->state = ANEM_DHCP_STATE_REBINDING;
            cl->attempts = 1;
            cl->next_ms = add_ms(now_ms,
                                 renew_interval_ms(cl->lease_at_ms, now_ms));
            return ANEM_DHCP_OK;
        }
        if (now_ms >= cl->next_ms) {
            st = emit_request(cl, false, false, out, cap, out_len);
            if (st != ANEM_DHCP_OK) {
                return st;
            }
            cl->attempts++;
            cl->next_ms = add_ms(now_ms,
                                 renew_interval_ms(cl->t2_at_ms, now_ms));
        }
        return ANEM_DHCP_OK;
    case ANEM_DHCP_STATE_REBINDING:
        if (now_ms >= cl->lease_at_ms) {
            return restart(cl, now_ms, out, cap, out_len);
        }
        if (now_ms >= cl->next_ms) {
            st = emit_request(cl, false, true, out, cap, out_len);
            if (st != ANEM_DHCP_OK) {
                return st;
            }
            cl->attempts++;
            cl->next_ms = add_ms(now_ms,
                                 renew_interval_ms(cl->lease_at_ms, now_ms));
        }
        return ANEM_DHCP_OK;
    default:
        return ANEM_DHCP_BADSTATE;
    }
}
