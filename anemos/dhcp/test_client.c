/* Scenario suite for the DHCP client state machine.
 *
 * Every expected packet is checked by parsing it with the codec (the same one
 * the client used to build it) and every timer is checked explicitly, because
 * "it returned OK" says nothing about whether the client will retransmit at
 * the right moment.
 */
#include <stdio.h>
#include <string.h>

#include "dhcp_client.h"

static unsigned failures;

static void check(bool cond, const char *what)
{
    if (cond) {
        printf("PASS %s\n", what);
    } else {
        printf("FAIL %s\n", what);
        failures++;
    }
}

static const uint8_t k_mac16[ANEM_DHCP_CHADDR_SIZE] = {
    0x00, 0x0b, 0x82, 0x01, 0xfc, 0x42, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
};

static const uint32_t k_ip = 0xc0a80164u;     /* 192.168.1.100 */
static const uint32_t k_server = 0xc0a80101u;  /* 192.168.1.1 */
static const uint32_t k_server2 = 0xc0a80102u;

struct reply {
    uint32_t xid;
    uint8_t op;
    uint8_t type;
    uint32_t yiaddr;
    uint32_t server;
    bool with_server;
    uint32_t lease;
    bool with_lease;
    uint32_t t1;
    uint32_t t2;
    bool with_t1t2;
    const uint8_t *chaddr;
    uint8_t hlen;
};

static bool make_reply(const struct reply *r, uint8_t *buf, size_t cap,
                       size_t *len)
{
    AnemDhcpHeader hdr;
    AnemDhcpWriter w;

    if (anem_dhcp_header_init(&hdr) != ANEM_DHCP_OK) {
        return false;
    }
    hdr.op = r->op;
    hdr.hlen = r->hlen;
    hdr.xid = r->xid;
    hdr.yiaddr = r->yiaddr;
    hdr.siaddr = r->server;
    memcpy(hdr.chaddr, r->chaddr, ANEM_DHCP_CHADDR_SIZE);
    if (anem_dhcp_build_begin(&w, buf, cap, &hdr) != ANEM_DHCP_OK
        || anem_dhcp_build_option_u8(&w, ANEM_DHCP_OPT_MESSAGE_TYPE, r->type)
               != ANEM_DHCP_OK) {
        return false;
    }
    if (r->with_server
        && anem_dhcp_build_option_u32(&w, ANEM_DHCP_OPT_SERVER_ID, r->server)
               != ANEM_DHCP_OK) {
        return false;
    }
    if (r->with_lease
        && anem_dhcp_build_option_u32(&w, ANEM_DHCP_OPT_LEASE_TIME, r->lease)
               != ANEM_DHCP_OK) {
        return false;
    }
    if (r->with_t1t2
        && (anem_dhcp_build_option_u32(&w, ANEM_DHCP_OPT_T1, r->t1)
                != ANEM_DHCP_OK
            || anem_dhcp_build_option_u32(&w, ANEM_DHCP_OPT_T2, r->t2)
                   != ANEM_DHCP_OK)) {
        return false;
    }
    return anem_dhcp_build_finish(&w, len) == ANEM_DHCP_OK;
}

static struct reply reply(uint32_t xid, uint8_t type)
{
    struct reply r;

    memset(&r, 0, sizeof r);
    r.xid = xid;
    r.op = ANEM_DHCP_BOOTREPLY;
    r.type = type;
    r.yiaddr = k_ip;
    r.server = k_server;
    r.with_server = true;
    r.chaddr = k_mac16;
    r.hlen = 6;
    return r;
}

static bool packet_is(const uint8_t *buf, size_t len, uint8_t type,
                      uint32_t ciaddr, bool broadcast, uint32_t xid)
{
    AnemDhcpMessage msg;
    uint8_t got = 0;

    if (anem_dhcp_parse(buf, len, &msg) != ANEM_DHCP_OK) {
        return false;
    }
    if (msg.header.op != ANEM_DHCP_BOOTREQUEST
        || msg.header.xid != xid || msg.header.ciaddr != ciaddr) {
        return false;
    }
    if (((msg.header.flags & ANEM_DHCP_FLAG_BROADCAST) != 0u) != broadcast) {
        return false;
    }
    if (anem_dhcp_option_u8(&msg, ANEM_DHCP_OPT_MESSAGE_TYPE, &got)
            != ANEM_DHCP_OK) {
        return false;
    }
    return got == type;
}

static bool offer_for(const AnemDhcpClient *cl, uint8_t *buf, size_t cap,
                      size_t *len)
{
    struct reply r = reply(cl->xid, ANEM_DHCP_OFFER);

    return make_reply(&r, buf, cap, len);
}

static bool ack_for(const AnemDhcpClient *cl, uint32_t lease, uint8_t *buf,
                    size_t cap, size_t *len)
{
    struct reply r = reply(cl->xid, ANEM_DHCP_ACK);

    r.lease = lease;
    r.with_lease = true;
    return make_reply(&r, buf, cap, len);
}

static void scenario_discover_and_offer(void)
{
    AnemDhcpClient cl;
    uint8_t out[512];
    uint8_t in[512];
    size_t out_len = 1;
    size_t in_len = 0;
    uint32_t first_xid;

    check(anem_dhcp_client_init(&cl, 1u, k_mac16, 6) == ANEM_DHCP_OK,
          "init");
    check(cl.state == ANEM_DHCP_STATE_INIT && cl.hlen == 6u
          && cl.chaddr[5] == 0x42u, "init fields");
    check(anem_dhcp_client_tick(&cl, 0, out, sizeof out, &out_len)
              == ANEM_DHCP_OK && out_len == 0u,
          "tick in INIT sends nothing");

    check(anem_dhcp_client_start(&cl, 0, out, sizeof out, &out_len)
              == ANEM_DHCP_OK, "start");
    check(cl.state == ANEM_DHCP_STATE_SELECTING && cl.attempts == 1u
          && cl.next_ms == ANEM_DHCP_RETRANSMIT_MS,
          "start moves to SELECTING with a 4 s deadline");
    check(packet_is(out, out_len, ANEM_DHCP_DISCOVER, 0u, true, cl.xid),
          "start emits a broadcast DISCOVER");
    first_xid = cl.xid;
    check(first_xid != 0u, "xid is generated");
    check(anem_dhcp_client_start(&cl, 0, out, sizeof out, &out_len)
              == ANEM_DHCP_BADSTATE, "second start is BADSTATE");

    check(anem_dhcp_client_tick(&cl, 3999u, out, sizeof out, &out_len)
              == ANEM_DHCP_OK && out_len == 0u,
          "no retransmit before the deadline");
    check(anem_dhcp_client_tick(&cl, 4000u, out, sizeof out, &out_len)
              == ANEM_DHCP_OK, "retransmit at the deadline");
    check(cl.attempts == 2u && cl.next_ms == 12000u
          && packet_is(out, out_len, ANEM_DHCP_DISCOVER, 0u, true, first_xid),
          "retransmit keeps the xid, backs off to 8 s");

    /* An OFFER with the wrong xid is ignored. */
    if (offer_for(&cl, in, sizeof in, &in_len)) {
        AnemDhcpClient other;

        other = cl;
        other.xid = (uint32_t)cl.xid + 1u;
        check(anem_dhcp_client_receive(&other, 5000u, in, in_len, out,
                                       sizeof out, &out_len)
                  == ANEM_DHCP_OK && out_len == 0u,
              "OFFER for another xid is ignored");
    }

    check(offer_for(&cl, in, sizeof in, &in_len), "build OFFER");
    check(anem_dhcp_client_receive(&cl, 5000u, in, in_len, out, sizeof out,
                                   &out_len) == ANEM_DHCP_OK,
          "receive OFFER");
    check(cl.state == ANEM_DHCP_STATE_REQUESTING && cl.offered_ip == k_ip
          && cl.server_id == k_server, "OFFER fills the request fields");
    check(packet_is(out, out_len, ANEM_DHCP_REQUEST, 0u, true, first_xid),
          "OFFER triggers a broadcast REQUEST");
}

static void scenario_ack_and_renew(void)
{
    AnemDhcpClient cl;
    uint8_t out[512];
    uint8_t in[512];
    size_t out_len = 0;
    size_t in_len = 0;

    (void)anem_dhcp_client_init(&cl, 1u, k_mac16, 6);
    (void)anem_dhcp_client_start(&cl, 0, out, sizeof out, &out_len);
    (void)offer_for(&cl, in, sizeof in, &in_len);
    (void)anem_dhcp_client_receive(&cl, 5000u, in, in_len, out, sizeof out,
                                   &out_len);

    check(ack_for(&cl, 3600u, in, sizeof in, &in_len), "build ACK");
    check(anem_dhcp_client_receive(&cl, 6000u, in, in_len, out, sizeof out,
                                   &out_len) == ANEM_DHCP_OK,
          "receive ACK");
    check(cl.state == ANEM_DHCP_STATE_BOUND && cl.ip == k_ip
          && cl.lease_secs == 3600u, "ACK binds the lease");
    check(cl.t1_secs == 1800u && cl.t2_secs == 3150u,
          "T1/T2 default to lease/2 and 7/8");
    check(cl.t1_at_ms == 1806000u && cl.t2_at_ms == 3156000u
          && cl.lease_at_ms == 3606000u, "absolute deadlines");
    check(out_len == 0u, "ACK sends nothing");

    check(anem_dhcp_client_tick(&cl, 1805999u, out, sizeof out, &out_len)
              == ANEM_DHCP_OK && out_len == 0u,
          "nothing before T1");
    check(anem_dhcp_client_tick(&cl, 1806000u, out, sizeof out, &out_len)
              == ANEM_DHCP_OK, "T1 triggers renewal");
    check(cl.state == ANEM_DHCP_STATE_RENEWING
          && packet_is(out, out_len, ANEM_DHCP_REQUEST, k_ip, false, cl.xid),
          "renewal is unicast with ciaddr set");
    check(cl.next_ms == 2481000u, "renewal retransmit is half-way to T2");

    check(anem_dhcp_client_tick(&cl, 2481000u, out, sizeof out, &out_len)
              == ANEM_DHCP_OK
          && packet_is(out, out_len, ANEM_DHCP_REQUEST, k_ip, false, cl.xid),
          "renewal retransmits at half the remaining time");

    /* An ACK during renewal rebinds with fresh timers. */
    check(ack_for(&cl, 3600u, in, sizeof in, &in_len), "build renewal ACK");
    check(anem_dhcp_client_receive(&cl, 2500000u, in, in_len, out,
                                   sizeof out, &out_len) == ANEM_DHCP_OK,
          "receive renewal ACK");
    check(cl.state == ANEM_DHCP_STATE_BOUND
          && cl.t1_at_ms == 4300000u && cl.lease_at_ms == 6100000u,
          "renewal ACK restarts the lease clock");
}

static void scenario_rebind_and_expiry(void)
{
    AnemDhcpClient cl;
    uint8_t out[512];
    uint8_t in[512];
    size_t out_len = 0;
    size_t in_len = 0;

    (void)anem_dhcp_client_init(&cl, 7u, k_mac16, 6);
    (void)anem_dhcp_client_start(&cl, 0, out, sizeof out, &out_len);
    (void)offer_for(&cl, in, sizeof in, &in_len);
    (void)anem_dhcp_client_receive(&cl, 1000u, in, in_len, out, sizeof out,
                                   &out_len);
    (void)ack_for(&cl, 100u, in, sizeof in, &in_len);
    (void)anem_dhcp_client_receive(&cl, 2000u, in, in_len, out, sizeof out,
                                   &out_len);
    /* t1 = 50 s -> 52000 ms; t2 = 87 s (7/8 of 100 = 87) -> 89000 ms;
     * lease 100 s -> 102000 ms. */

    check(anem_dhcp_client_tick(&cl, 52000u, out, sizeof out, &out_len)
              == ANEM_DHCP_OK && cl.state == ANEM_DHCP_STATE_RENEWING,
          "enter RENEWING");
    check(anem_dhcp_client_tick(&cl, 89000u, out, sizeof out, &out_len)
              == ANEM_DHCP_OK
          && packet_is(out, out_len, ANEM_DHCP_REQUEST, k_ip, true, cl.xid),
          "T2 rebinds by broadcast");
    check(cl.state == ANEM_DHCP_STATE_REBINDING, "enter REBINDING");

    check(anem_dhcp_client_tick(&cl, 101999u, out, sizeof out, &out_len)
              == ANEM_DHCP_OK && out_len == 0u,
          "nothing just before lease expiry");
    {
        uint32_t old_xid = cl.xid;

        check(anem_dhcp_client_tick(&cl, 102000u, out, sizeof out, &out_len)
                  == ANEM_DHCP_OK,
              "lease expiry restarts");
        check(cl.state == ANEM_DHCP_STATE_SELECTING && cl.xid != old_xid
              && packet_is(out, out_len, ANEM_DHCP_DISCOVER, 0u, true, cl.xid),
              "expiry emits a DISCOVER with a new xid");
    }
}

static void scenario_nak(void)
{
    AnemDhcpClient cl;
    uint8_t out[512];
    uint8_t in[512];
    size_t out_len = 0;
    size_t in_len = 0;
    uint32_t old_xid;

    (void)anem_dhcp_client_init(&cl, 99u, k_mac16, 6);
    (void)anem_dhcp_client_start(&cl, 0, out, sizeof out, &out_len);
    (void)offer_for(&cl, in, sizeof in, &in_len);
    (void)anem_dhcp_client_receive(&cl, 1000u, in, in_len, out, sizeof out,
                                   &out_len);
    (void)ack_for(&cl, 3600u, in, sizeof in, &in_len);
    (void)anem_dhcp_client_receive(&cl, 2000u, in, in_len, out, sizeof out,
                                   &out_len);
    (void)anem_dhcp_client_tick(&cl, 1802000u, out, sizeof out, &out_len);
    check(cl.state == ANEM_DHCP_STATE_RENEWING, "NAK setup: RENEWING");

    old_xid = cl.xid;
    {
        struct reply r = reply(cl.xid, ANEM_DHCP_NAK);

        r.yiaddr = 0;
        check(make_reply(&r, in, sizeof in, &in_len), "build NAK");
    }
    check(anem_dhcp_client_receive(&cl, 1803000u, in, in_len, out, sizeof out,
                                   &out_len) == ANEM_DHCP_OK,
          "receive NAK");
    check(cl.state == ANEM_DHCP_STATE_SELECTING && cl.xid != old_xid
          && packet_is(out, out_len, ANEM_DHCP_DISCOVER, 0u, true, cl.xid),
          "NAK restarts with a new DISCOVER");
}

static void scenario_ignored(void)
{
    AnemDhcpClient cl;
    uint8_t out[512];
    uint8_t in[512];
    size_t out_len = 0;
    size_t in_len = 0;
    AnemDhcpStatus st;
    struct reply r;

    (void)anem_dhcp_client_init(&cl, 5u, k_mac16, 6);
    (void)anem_dhcp_client_start(&cl, 0, out, sizeof out, &out_len);

    /* wrong chaddr */
    r = reply(cl.xid, ANEM_DHCP_OFFER);
    r.chaddr = (const uint8_t[]){ 0xde, 0xad, 0xbe, 0xef, 0, 0,
                                  0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
    check(make_reply(&r, in, sizeof in, &in_len)
          && anem_dhcp_client_receive(&cl, 1000u, in, in_len, out,
                                      sizeof out, &out_len) == ANEM_DHCP_OK
          && out_len == 0u, "OFFER for another chaddr is ignored");

    /* wrong opcode */
    r = reply(cl.xid, ANEM_DHCP_OFFER);
    r.op = ANEM_DHCP_BOOTREQUEST;
    check(make_reply(&r, in, sizeof in, &in_len)
          && anem_dhcp_client_receive(&cl, 1000u, in, in_len, out,
                                      sizeof out, &out_len) == ANEM_DHCP_OK
          && out_len == 0u, "BOOTREQUEST is ignored");

    /* no server identifier */
    r = reply(cl.xid, ANEM_DHCP_OFFER);
    r.with_server = false;
    check(make_reply(&r, in, sizeof in, &in_len)
          && anem_dhcp_client_receive(&cl, 1000u, in, in_len, out,
                                      sizeof out, &out_len) == ANEM_DHCP_OK
          && out_len == 0u, "OFFER without a server id is ignored");

    /* a malformed frame surfaces the codec status */
    memset(in, 0, 239);
    st = anem_dhcp_client_receive(&cl, 1000u, in, 239u, out, sizeof out,
                                  &out_len);
    check(st == ANEM_DHCP_SHORT && out_len == 0u,
          "malformed frame returns the codec status");
    check(cl.state == ANEM_DHCP_STATE_SELECTING, "ignored frames do not move");
}

static void scenario_bad_ack(void)
{
    AnemDhcpClient cl;
    uint8_t out[512];
    uint8_t in[512];
    size_t out_len = 0;
    size_t in_len = 0;
    struct reply r;

    (void)anem_dhcp_client_init(&cl, 11u, k_mac16, 6);
    (void)anem_dhcp_client_start(&cl, 0, out, sizeof out, &out_len);
    (void)offer_for(&cl, in, sizeof in, &in_len);
    (void)anem_dhcp_client_receive(&cl, 1000u, in, in_len, out, sizeof out,
                                   &out_len);

    r = reply(cl.xid, ANEM_DHCP_ACK);
    r.lease = 3600u;
    r.with_lease = false;
    check(make_reply(&r, in, sizeof in, &in_len)
          && anem_dhcp_client_receive(&cl, 2000u, in, in_len, out,
                                      sizeof out, &out_len) == ANEM_DHCP_OK,
          "ACK without a lease is ignored");
    check(cl.state == ANEM_DHCP_STATE_REQUESTING, "state unchanged");

    r = reply(cl.xid, ANEM_DHCP_ACK);
    r.server = k_server2;
    r.lease = 3600u;
    r.with_lease = true;
    check(make_reply(&r, in, sizeof in, &in_len)
          && anem_dhcp_client_receive(&cl, 2000u, in, in_len, out,
                                      sizeof out, &out_len) == ANEM_DHCP_OK,
          "ACK from the wrong server is ignored");
    check(cl.state == ANEM_DHCP_STATE_REQUESTING, "state still REQUESTING");

    r = reply(cl.xid, ANEM_DHCP_ACK);
    r.yiaddr = k_ip + 1u;
    r.lease = 3600u;
    r.with_lease = true;
    check(make_reply(&r, in, sizeof in, &in_len)
          && anem_dhcp_client_receive(&cl, 2000u, in, in_len, out,
                                      sizeof out, &out_len) == ANEM_DHCP_OK,
          "ACK for a different address is ignored");
}

static void scenario_nospace(void)
{
    AnemDhcpClient cl;
    uint8_t out[512];
    size_t out_len = 0;

    (void)anem_dhcp_client_init(&cl, 3u, k_mac16, 6);
    check(anem_dhcp_client_start(&cl, 0, out, 16u, &out_len)
              == ANEM_DHCP_NOSPACE,
          "start reports a short buffer");
    check(cl.state == ANEM_DHCP_STATE_INIT && out_len == 0u,
          "failed start leaves the client in INIT");
    check(anem_dhcp_client_start(&cl, 0, out, sizeof out, &out_len)
              == ANEM_DHCP_OK, "start succeeds once the buffer is big enough");
}

static void scenario_contracts(void)
{
    AnemDhcpClient cl;
    uint8_t out[512];
    uint8_t in[512];
    size_t len = 0;

    check(anem_dhcp_client_init(NULL, 1u, k_mac16, 6) == ANEM_DHCP_BADARG,
          "init NULL client");
    check(anem_dhcp_client_init(&cl, 1u, NULL, 6) == ANEM_DHCP_BADARG,
          "init NULL chaddr");
    check(anem_dhcp_client_init(&cl, 1u, k_mac16, 0) == ANEM_DHCP_BADARG,
          "init hlen 0");
    check(anem_dhcp_client_init(&cl, 1u, k_mac16, 17) == ANEM_DHCP_BADARG,
          "init hlen 17");
    (void)anem_dhcp_client_init(&cl, 1u, k_mac16, 6);
    check(anem_dhcp_client_start(NULL, 0, out, sizeof out, &len)
              == ANEM_DHCP_BADARG, "start NULL client");
    check(anem_dhcp_client_start(&cl, 0, NULL, 0, &len) == ANEM_DHCP_BADARG,
          "start NULL buffer");
    check(anem_dhcp_client_start(&cl, 0, out, sizeof out, NULL)
              == ANEM_DHCP_BADARG, "start NULL length");
    check(anem_dhcp_client_tick(NULL, 0, out, sizeof out, &len)
              == ANEM_DHCP_BADARG, "tick NULL client");
    check(anem_dhcp_client_receive(&cl, 0, NULL, 0, out, sizeof out, &len)
              == ANEM_DHCP_BADARG, "receive NULL wire");
    memset(in, 0, sizeof in);
    check(anem_dhcp_client_receive(&cl, 0, in, sizeof in, NULL, 0, &len)
              == ANEM_DHCP_BADARG, "receive NULL buffer");
}

int main(void)
{
    scenario_discover_and_offer();
    scenario_ack_and_renew();
    scenario_rebind_and_expiry();
    scenario_nak();
    scenario_ignored();
    scenario_bad_ack();
    scenario_nospace();
    scenario_contracts();

    if (failures != 0u) {
        printf("FAILED: %u check(s)\n", failures);
        return 1;
    }
    printf("all DHCP client scenarios pass\n");
    return 0;
}
