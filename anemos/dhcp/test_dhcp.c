/* Vector suite for anemos/dhcp.
 *
 * Golden messages are authored from RFC 2131/2132 in dhcp_ref.py (field
 * values and option bytes) and embedded here as literal wire bytes. Each one
 * must parse, write back byte-identically, and expose the expected fields.
 * The malformed list pins one status per failure class.
 */
#include <stdio.h>
#include <string.h>

#include "dhcp.h"

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

static int hex_nibble(char c)
{
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f') {
        return c - 'a' + 10;
    }
    if (c >= 'A' && c <= 'F') {
        return c - 'A' + 10;
    }
    return -1;
}

static bool hex_decode(const char *hex, uint8_t *out, size_t cap, size_t *len)
{
    size_t n = strlen(hex);

    if ((n & 1u) != 0u || n / 2u > cap) {
        return false;
    }
    for (size_t i = 0; i < n; i += 2u) {
        int hi = hex_nibble(hex[i]);
        int lo = hex_nibble(hex[i + 1u]);

        if (hi < 0 || lo < 0) {
            return false;
        }
        out[i / 2u] = (uint8_t)(((unsigned)hi << 4) | (unsigned)lo);
    }
    *len = n / 2u;
    return true;
}

static const struct golden {
    const char *name;
    size_t len;
    const char *hex;
} k_golden[] = {
    { "discover", 258,
      "010106003903f3260000800000000000000000000000000000000000000b8201fc420000"
      "000000000000000000000000000000000000000000000000000000000000000000000000"
      "000000000000000000000000000000000000000000000000000000000000000000000000"
      "000000000000000000000000000000000000000000000000000000000000000000000000"
      "000000000000000000000000000000000000000000000000000000000000000000000000"
      "000000000000000000000000000000000000000000000000000000000000000000000000"
      "00000000000000000000000000000000000000006382536335010137030103063d070100"
      "0b8201fc42ff" },
    { "offer", 278,
      "020106003903f3260000000000000000c0a80164c0a8010100000000000b8201fc420000"
      "000000000000000000000000000000000000000000000000000000000000000000000000"
      "000000000000000000000000000000000000000000000000000000000000000000000000"
      "000000000000000000000000000000000000000000000000000000000000000000000000"
      "000000000000000000000000000000000000000000000000000000000000000000000000"
      "000000000000000000000000000000000000000000000000000000000000000000000000"
      "0000000000000000000000000000000000000000638253633501023604c0a80101330400"
      "000e100104ffffff000304c0a8010106080808080808080404ff" },
    { "request", 265,
      "01010600123456780000800000000000000000000000000000000000000b8201fc420000"
      "000000000000000000000000000000000000000000000000000000000000000000000000"
      "000000000000000000000000000000000000000000000000000000000000000000000000"
      "000000000000000000000000000000000000000000000000000000000000000000000000"
      "000000000000000000000000000000000000000000000000000000000000000000000000"
      "000000000000000000000000000000000000000000000000000000000000000000000000"
      "0000000000000000000000000000000000000000638253633501033204c0a801643604c0"
      "a801013d0701000b8201fc42ff" },
    { "ack", 268,
      "02010600123456780000000000000000c0a80164c0a8010100000000000b8201fc420000"
      "000000000000000000000000000000000000000000000000000000000000000000000000"
      "000000000000000000000000000000000000000000000000000000000000000000000000"
      "000000000000000000000000000000000000000000000000000000000000000000000000"
      "000000000000000000000000000000000000000000000000000000000000000000000000"
      "000000000000000000000000000000000000000000000000000000000000000000000000"
      "0000000000000000000000000000000000000000638253633501053604c0a80101330400"
      "000e103a04000007083b0400000a8cff" },
    { "nak", 264,
      "02010600123456780000000000000000000000000000000000000000000b8201fc420000"
      "000000000000000000000000000000000000000000000000000000000000000000000000"
      "000000000000000000000000000000000000000000000000000000000000000000000000"
      "000000000000000000000000000000000000000000000000000000000000000000000000"
      "000000000000000000000000000000000000000000000000000000000000000000000000"
      "000000000000000000000000000000000000000000000000000000000000000000000000"
      "0000000000000000000000000000000000000000638253633501063604c0a80101380c77"
      "726f6e67207375626e6574ff" },
    { "release", 250,
      "010106001234567800000000c0a80164000000000000000000000000000b8201fc420000"
      "000000000000000000000000000000000000000000000000000000000000000000000000"
      "000000000000000000000000000000000000000000000000000000000000000000000000"
      "000000000000000000000000000000000000000000000000000000000000000000000000"
      "000000000000000000000000000000000000000000000000000000000000000000000000"
      "000000000000000000000000000000000000000000000000000000000000000000000000"
      "0000000000000000000000000000000000000000638253633501073604c0a80101ff" },
    { "inform", 249,
      "010106001234567800000000c0a80164000000000000000000000000000b8201fc420000"
      "000000000000000000000000000000000000000000000000000000000000000000000000"
      "000000000000000000000000000000000000000000000000000000000000000000000000"
      "000000000000000000000000000000000000000000000000000000000000000000000000"
      "000000000000000000000000000000000000000000000000000000000000000000000000"
      "000000000000000000000000000000000000000000000000000000000000000000000000"
      "0000000000000000000000000000000000000000638253633501083703010306ff" },
    { "padded", 266,
      "010106003903f3260000800000000000000000000000000000000000000b8201fc420000"
      "000000000000000000000000000000000000000000000000000000000000000000000000"
      "000000000000000000000000000000000000000000000000000000000000000000000000"
      "000000000000000000000000000000000000000000000000000000000000000000000000"
      "000000000000000000000000000000000000000000000000000000000000000000000000"
      "000000000000000000000000000000000000000000000000000000000000000000000000"
      "000000000000000000000000000000000000000063825363003501010037030103063d07"
      "01000b8201fc4200ff0000000000" },
    { "minimal", 241,
      "01010600000000010000800000000000000000000000000000000000000b8201fc420000"
      "000000000000000000000000000000000000000000000000000000000000000000000000"
      "000000000000000000000000000000000000000000000000000000000000000000000000"
      "000000000000000000000000000000000000000000000000000000000000000000000000"
      "000000000000000000000000000000000000000000000000000000000000000000000000"
      "000000000000000000000000000000000000000000000000000000000000000000000000"
      "000000000000000000000000000000000000000063825363ff" },
};

static bool option_is(const AnemDhcpMessage *msg, uint8_t code, unsigned nth,
                      const void *want, uint8_t len)
{
    const AnemDhcpOption *opt = anem_dhcp_find_option(msg, code, nth);
    const uint8_t *data;

    if (opt == NULL || opt->len != len) {
        return false;
    }
    data = anem_dhcp_option_data(msg, opt);
    return data != NULL && memcmp(data, want, (size_t)len) == 0;
}

static void check_options(const AnemDhcpMessage *msg)
{
    for (unsigned i = 0; i < (unsigned)msg->option_count; i++) {
        const AnemDhcpOption *opt = &msg->options[i];

        if (opt->code == ANEM_DHCP_OPT_PAD) {
            check(opt->len == 0u && msg->wire[opt->off] == 0u,
                  "PAD option is a single zero byte");
        } else {
            check(anem_dhcp_option_data(msg, opt) != NULL,
                  "option data is in range");
        }
    }
}

static void check_specifics(const char *name, const AnemDhcpMessage *msg)
{
    uint8_t v8 = 0;
    uint32_t v32 = 0;
    static const uint8_t k_msg[] = {
        'w', 'r', 'o', 'n', 'g', ' ', 's', 'u', 'b', 'n', 'e', 't'
    };

    if (strcmp(name, "discover") == 0) {
        check(msg->header.op == ANEM_DHCP_BOOTREQUEST
              && msg->header.htype == ANEM_DHCP_HTYPE_ETHERNET
              && msg->header.hlen == 6u, "discover header");
        check(msg->header.xid == 0x3903f326u
              && msg->header.flags == ANEM_DHCP_FLAG_BROADCAST,
              "discover xid/flags");
        check(msg->header.chaddr[0] == 0x00u && msg->header.chaddr[1] == 0x0bu
              && msg->header.chaddr[5] == 0x42u, "discover chaddr");
        check(anem_dhcp_option_u8(msg, ANEM_DHCP_OPT_MESSAGE_TYPE, &v8)
                  == ANEM_DHCP_OK && v8 == ANEM_DHCP_DISCOVER,
              "discover message type");
        check(option_is(msg, ANEM_DHCP_OPT_PARAM_LIST, 0,
                        (const uint8_t[]){ 1, 3, 6 }, 3), "discover param list");
        check(anem_dhcp_option_u32(msg, ANEM_DHCP_OPT_LEASE_TIME, &v32)
                  == ANEM_DHCP_NOT_FOUND, "discover has no lease time");
    } else if (strcmp(name, "offer") == 0) {
        check(msg->header.op == ANEM_DHCP_BOOTREPLY
              && msg->header.yiaddr == 0xc0a80164u
              && msg->header.siaddr == 0xc0a80101u, "offer addresses");
        check(anem_dhcp_option_u8(msg, ANEM_DHCP_OPT_MESSAGE_TYPE, &v8)
                  == ANEM_DHCP_OK && v8 == ANEM_DHCP_OFFER, "offer type");
        check(anem_dhcp_option_u32(msg, ANEM_DHCP_OPT_LEASE_TIME, &v32)
                  == ANEM_DHCP_OK && v32 == 3600u, "offer lease");
        check(anem_dhcp_option_u32(msg, ANEM_DHCP_OPT_SERVER_ID, &v32)
                  == ANEM_DHCP_OK && v32 == 0xc0a80101u, "offer server id");
        check(option_is(msg, ANEM_DHCP_OPT_DNS, 0,
                        (const uint8_t[]){ 8, 8, 8, 8, 8, 8, 4, 4 }, 8),
              "offer dns list");
        check(anem_dhcp_option_u8(msg, ANEM_DHCP_OPT_LEASE_TIME, &v8)
                  == ANEM_DHCP_FORMAT, "wrong width is FORMAT");
    } else if (strcmp(name, "request") == 0) {
        check(anem_dhcp_option_u8(msg, ANEM_DHCP_OPT_MESSAGE_TYPE, &v8)
                  == ANEM_DHCP_OK && v8 == ANEM_DHCP_REQUEST, "request type");
        check(anem_dhcp_option_u32(msg, ANEM_DHCP_OPT_REQUESTED_IP, &v32)
                  == ANEM_DHCP_OK && v32 == 0xc0a80164u, "requested ip");
    } else if (strcmp(name, "ack") == 0) {
        check(anem_dhcp_option_u8(msg, ANEM_DHCP_OPT_MESSAGE_TYPE, &v8)
                  == ANEM_DHCP_OK && v8 == ANEM_DHCP_ACK, "ack type");
        check(anem_dhcp_option_u32(msg, ANEM_DHCP_OPT_T1, &v32)
                  == ANEM_DHCP_OK && v32 == 1800u, "ack T1");
        check(anem_dhcp_option_u32(msg, ANEM_DHCP_OPT_T2, &v32)
                  == ANEM_DHCP_OK && v32 == 2700u, "ack T2");
    } else if (strcmp(name, "nak") == 0) {
        check(anem_dhcp_option_u8(msg, ANEM_DHCP_OPT_MESSAGE_TYPE, &v8)
                  == ANEM_DHCP_OK && v8 == ANEM_DHCP_NAK, "nak type");
        check(option_is(msg, ANEM_DHCP_OPT_MESSAGE, 0, k_msg,
                        (uint8_t)sizeof k_msg), "nak message text");
    } else if (strcmp(name, "release") == 0) {
        check(msg->header.op == ANEM_DHCP_BOOTREQUEST
              && msg->header.ciaddr == 0xc0a80164u, "release ciaddr");
        check(anem_dhcp_option_u8(msg, ANEM_DHCP_OPT_MESSAGE_TYPE, &v8)
                  == ANEM_DHCP_OK && v8 == ANEM_DHCP_RELEASE, "release type");
    } else if (strcmp(name, "inform") == 0) {
        check(msg->header.ciaddr == 0xc0a80164u, "inform ciaddr");
        check(anem_dhcp_option_u8(msg, ANEM_DHCP_OPT_MESSAGE_TYPE, &v8)
                  == ANEM_DHCP_OK && v8 == ANEM_DHCP_INFORM, "inform type");
    } else if (strcmp(name, "padded") == 0) {
        check(msg->option_count == 6u && msg->pad_len == 5u,
              "padded counts");
        check(anem_dhcp_find_option(msg, ANEM_DHCP_OPT_PAD, 0) != NULL
              && anem_dhcp_find_option(msg, ANEM_DHCP_OPT_PAD, 1) != NULL
              && anem_dhcp_find_option(msg, ANEM_DHCP_OPT_PAD, 2) != NULL
              && anem_dhcp_find_option(msg, ANEM_DHCP_OPT_PAD, 3) == NULL,
              "padded PAD entries");
    } else if (strcmp(name, "minimal") == 0) {
        check(msg->option_count == 0u && msg->pad_len == 0u,
              "minimal has no options");
    }
}

static void check_golden(const struct golden *g, uint8_t *wire,
                         uint8_t *out)
{
    AnemDhcpMessage msg;
    AnemDhcpMessage again;
    size_t wire_len = 0;
    size_t out_len = 0;
    char label[160];

    (void)snprintf(label, sizeof label, "%s: hex", g->name);
    if (!hex_decode(g->hex, wire, 65535u, &wire_len)
        || wire_len != g->len) {
        check(false, label);
        return;
    }
    check(true, label);

    (void)snprintf(label, sizeof label, "%s: parses", g->name);
    if (anem_dhcp_parse(wire, wire_len, &msg) != ANEM_DHCP_OK) {
        check(false, label);
        return;
    }
    check(true, label);

    (void)snprintf(label, sizeof label, "%s: byte-identical write", g->name);
    if (anem_dhcp_write(&msg, out, 65535u, &out_len) != ANEM_DHCP_OK) {
        check(false, label);
    } else {
        check(out_len == wire_len && memcmp(out, wire, wire_len) == 0, label);
    }

    (void)snprintf(label, sizeof label, "%s: reparse", g->name);
    if (anem_dhcp_parse(out, out_len, &again) != ANEM_DHCP_OK) {
        check(false, label);
    } else {
        check(again.header.xid == msg.header.xid
              && again.header.op == msg.header.op
              && again.header.yiaddr == msg.header.yiaddr
              && again.option_count == msg.option_count
              && again.pad_len == msg.pad_len, label);
    }

    (void)snprintf(label, sizeof label, "%s: exact-fit write", g->name);
    check(anem_dhcp_write(&msg, out, wire_len, &out_len) == ANEM_DHCP_OK,
          label);

    (void)snprintf(label, sizeof label, "%s: short buffer rejected", g->name);
    check(anem_dhcp_write(&msg, out, wire_len - 1u, &out_len)
              == ANEM_DHCP_NOSPACE, label);

    check_options(&msg);
    check_specifics(g->name, &msg);
}

/* The minimal golden message is header + cookie + END (241 bytes). The
 * malformed cases are derived from it, so their construction is independent
 * of the codec. */
static bool load_minimal(uint8_t *dst, size_t *len)
{
    for (size_t i = 0; i < sizeof k_golden / sizeof k_golden[0]; i++) {
        if (strcmp(k_golden[i].name, "minimal") == 0) {
            return hex_decode(k_golden[i].hex, dst, 65535u, len);
        }
    }
    return false;
}

static void check_malformed(uint8_t *wire)
{
    AnemDhcpMessage msg;
    size_t n = 0;

    check(anem_dhcp_parse("", 0, &msg) == ANEM_DHCP_SHORT, "malformed empty");
    {
        static uint8_t shortbuf[239];

        memset(shortbuf, 0, sizeof shortbuf);
        check(anem_dhcp_parse(shortbuf, sizeof shortbuf, &msg)
                  == ANEM_DHCP_SHORT, "malformed short_239");
    }

    if (!load_minimal(wire, &n)) {
        check(false, "malformed setup");
        return;
    }

    check(anem_dhcp_parse(wire, 240u, &msg) == ANEM_DHCP_NO_END,
          "malformed no_end_header_only");

    wire[2] = 17; /* hlen */
    check(anem_dhcp_parse(wire, n, &msg) == ANEM_DHCP_BAD_HLEN,
          "malformed bad_hlen");
    wire[2] = 6;

    wire[236] = 0x64;
    check(anem_dhcp_parse(wire, n, &msg) == ANEM_DHCP_BAD_COOKIE,
          "malformed bad_cookie");
    wire[236] = 0x63;

    /* 65 PAD entries exceed the 64-option view. */
    (void)load_minimal(wire, &n);
    memset(wire + 240u, 0, 65u);
    wire[305] = 0xffu;
    check(anem_dhcp_parse(wire, 306u, &msg) == ANEM_DHCP_TOO_MANY,
          "malformed too_many_options");

    /* non-zero byte after END */
    (void)load_minimal(wire, &n);
    wire[n] = 0x01u;
    check(anem_dhcp_parse(wire, n + 1u, &msg) == ANEM_DHCP_TRAILING,
          "malformed trailing_nonzero");

    /* option length runs past the end: 35 05 01 ff */
    (void)load_minimal(wire, &n);
    wire[240] = 0x35u;
    wire[241] = 0x05u;
    wire[242] = 0x01u;
    wire[243] = 0xffu;
    check(anem_dhcp_parse(wire, 244u, &msg) == ANEM_DHCP_BAD_OPTION,
          "malformed option_past_end");

    /* option with no length byte before END */
    (void)load_minimal(wire, &n);
    wire[240] = 0x35u;
    wire[241] = 0xffu;
    check(anem_dhcp_parse(wire, 242u, &msg) == ANEM_DHCP_BAD_OPTION,
          "malformed option_no_length");

    /* options end without END */
    (void)load_minimal(wire, &n);
    wire[240] = 0x35u;
    wire[241] = 0x01u;
    wire[242] = 0x01u;
    check(anem_dhcp_parse(wire, 243u, &msg) == ANEM_DHCP_NO_END,
          "malformed no_end_with_option");
}

static void check_builder(uint8_t *out, size_t out_cap)
{
    static const struct golden *discover;
    static const struct golden *minimal;
    AnemDhcpHeader hdr;
    AnemDhcpWriter w;
    size_t len = 0;
    size_t want_len = 0;
    static uint8_t want[512];

    discover = NULL;
    minimal = NULL;
    for (size_t i = 0; i < sizeof k_golden / sizeof k_golden[0]; i++) {
        if (strcmp(k_golden[i].name, "discover") == 0) {
            discover = &k_golden[i];
        }
        if (strcmp(k_golden[i].name, "minimal") == 0) {
            minimal = &k_golden[i];
        }
    }
    check(discover != NULL && minimal != NULL, "builder setup");

    check(anem_dhcp_header_init(NULL) == ANEM_DHCP_BADARG,
          "header_init NULL");

    /* Build the DISCOVER golden message. */
    if (discover != NULL && hex_decode(discover->hex, want, sizeof want,
                                       &want_len)) {
        static const uint8_t param[] = { 1, 3, 6 };
        static const uint8_t client[] = { 1, 0, 0x0b, 0x82, 0x01, 0xfc, 0x42 };
        static const uint8_t type = ANEM_DHCP_DISCOVER;
        static const uint8_t chaddr[] = { 0, 0x0b, 0x82, 0x01, 0xfc, 0x42 };

        check(anem_dhcp_header_init(&hdr) == ANEM_DHCP_OK,
              "header_init defaults");
        hdr.xid = 0x3903f326u;
        hdr.flags = ANEM_DHCP_FLAG_BROADCAST;
        memcpy(hdr.chaddr, chaddr, sizeof chaddr);
        check(anem_dhcp_build_begin(&w, out, out_cap, &hdr) == ANEM_DHCP_OK,
              "builder begin");
        check(anem_dhcp_build_option(&w, ANEM_DHCP_OPT_MESSAGE_TYPE,
                                     &type, 1) == ANEM_DHCP_OK,
              "builder type option");
        check(anem_dhcp_build_option(&w, ANEM_DHCP_OPT_PARAM_LIST, param,
                                     3) == ANEM_DHCP_OK,
              "builder param option");
        check(anem_dhcp_build_option(&w, ANEM_DHCP_OPT_CLIENT_ID, client,
                                     (uint8_t)sizeof client) == ANEM_DHCP_OK,
              "builder client option");
        check(anem_dhcp_build_finish(&w, &len) == ANEM_DHCP_OK,
              "builder finish");
        check(len == want_len && memcmp(out, want, want_len) == 0,
              "builder reproduces the DISCOVER golden vector");
    } else {
        check(false, "builder discover vector");
    }

    /* Build the minimal message. */
    if (minimal != NULL && hex_decode(minimal->hex, want, sizeof want,
                                      &want_len)) {
        static const uint8_t chaddr[] = { 0, 0x0b, 0x82, 0x01, 0xfc, 0x42 };

        check(anem_dhcp_header_init(&hdr) == ANEM_DHCP_OK,
              "minimal header_init");
        hdr.xid = 1;
        hdr.flags = ANEM_DHCP_FLAG_BROADCAST;
        memcpy(hdr.chaddr, chaddr, sizeof chaddr);
        check(anem_dhcp_build_begin(&w, out, out_cap, &hdr) == ANEM_DHCP_OK,
              "minimal begin");
        check(anem_dhcp_build_finish(&w, &len) == ANEM_DHCP_OK,
              "minimal finish");
        check(len == want_len && memcmp(out, want, want_len) == 0,
              "builder reproduces the minimal golden vector");
    } else {
        check(false, "builder minimal vector");
    }

    check(anem_dhcp_build_begin(&w, out, 4u, &hdr) == ANEM_DHCP_NOSPACE,
          "builder short buffer rejected");
    check(anem_dhcp_build_begin(NULL, out, out_cap, &hdr)
              == ANEM_DHCP_BADARG, "builder NULL writer");
    check(anem_dhcp_build_finish(&w, &len) == ANEM_DHCP_BADARG,
          "finish before begin is rejected");

    if (anem_dhcp_header_init(&hdr) == ANEM_DHCP_OK
        && anem_dhcp_build_begin(&w, out, out_cap, &hdr) == ANEM_DHCP_OK) {
        static const uint8_t one = 1;

        check(anem_dhcp_build_option(&w, ANEM_DHCP_OPT_PAD, NULL, 0)
                  == ANEM_DHCP_BADARG, "PAD is not a builder option");
        check(anem_dhcp_build_option(&w, ANEM_DHCP_OPT_END, NULL, 0)
                  == ANEM_DHCP_BADARG, "END is not a builder option");
        check(anem_dhcp_build_option(&w, 53, NULL, 1) == ANEM_DHCP_BADARG,
              "NULL data with length is rejected");
        check(anem_dhcp_build_option(&w, 53, &one, 1) == ANEM_DHCP_OK,
              "builder accepts a normal option");
    }
}

static void check_contracts(uint8_t *wire, size_t len)
{
    AnemDhcpMessage msg;
    AnemDhcpHeader hdr;
    AnemDhcpWriter w;
    const AnemDhcpOption *opt;
    uint8_t buf[512];
    uint8_t v = 0;
    size_t out_len = 0;

    memset(&msg, 0, sizeof msg);
    memset(&hdr, 0, sizeof hdr);
    check(anem_dhcp_parse(NULL, len, &msg) == ANEM_DHCP_BADARG,
          "parse NULL wire");
    check(anem_dhcp_parse(wire, len, NULL) == ANEM_DHCP_BADARG,
          "parse NULL message");
    check(anem_dhcp_write(NULL, buf, sizeof buf, &out_len)
              == ANEM_DHCP_BADARG, "write NULL message");
    check(anem_dhcp_write(&msg, NULL, sizeof buf, &out_len)
              == ANEM_DHCP_BADARG, "write NULL buffer");
    check(anem_dhcp_write(&msg, buf, sizeof buf, NULL) == ANEM_DHCP_BADARG,
          "write NULL length");
    check(anem_dhcp_find_option(NULL, 53, 0) == NULL,
          "find NULL message");
    check(anem_dhcp_option_u8(NULL, 53, &v) == ANEM_DHCP_BADARG,
          "typed reader on NULL message");
    check(anem_dhcp_option_u8(&msg, 53, NULL) == ANEM_DHCP_BADARG,
          "typed reader NULL output");
    check(anem_dhcp_build_begin(&w, NULL, 0, &hdr) == ANEM_DHCP_BADARG,
          "build_begin NULL buffer");
    check(anem_dhcp_build_option(NULL, 53, &v, 1) == ANEM_DHCP_BADARG,
          "build_option NULL writer");
    check(anem_dhcp_build_finish(NULL, &out_len) == ANEM_DHCP_BADARG,
          "build_finish NULL writer");

    if (anem_dhcp_parse(wire, len, &msg) == ANEM_DHCP_OK) {
        opt = anem_dhcp_find_option(&msg, ANEM_DHCP_OPT_END, 0);
        check(opt == NULL, "END is not stored as an option");
        opt = anem_dhcp_find_option(&msg, ANEM_DHCP_OPT_PAD, 0);
        check(opt == NULL || anem_dhcp_option_data(&msg, opt) == NULL,
              "PAD has no data");
    } else {
        check(false, "contracts setup");
    }
}

int main(void)
{
    static uint8_t wire[65536];
    static uint8_t out[65536];
    static uint8_t huge[65537];
    static uint8_t minimal[65536];
    size_t n = 0;
    AnemDhcpMessage msg;

    for (size_t i = 0; i < sizeof k_golden / sizeof k_golden[0]; i++) {
        check_golden(&k_golden[i], wire, out);
    }
    check_malformed(wire);
    check_builder(out, sizeof out);
    check(anem_dhcp_parse(huge, sizeof huge, &msg) == ANEM_DHCP_TOO_LONG,
          "message over 65535 rejected");

    if (load_minimal(minimal, &n)) {
        check_contracts(minimal, n);
    } else {
        check(false, "contracts setup");
    }

    /* hlen 16 is the maximum the fixed chaddr field can describe. */
    if (load_minimal(wire, &n)) {
        wire[2] = 16;
        check(anem_dhcp_parse(wire, n, &msg) == ANEM_DHCP_OK,
              "hlen 16 accepted");
    }

    if (failures != 0u) {
        printf("FAILED: %u check(s)\n", failures);
        return 1;
    }
    printf("all DHCP vectors pass\n");
    return 0;
}
