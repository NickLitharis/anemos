/* Vector suite for anemos/dns.
 *
 * The golden messages are written out by hand from RFC 1035 and are not
 * produced by the codec under test. Each one must:
 *   - parse,
 *   - serialise back to exactly the same bytes (deterministic compression),
 *   - parse again to the same header,
 *   - yield the expected names through the accessors,
 *   - survive presentation-form conversion.
 *
 * The malformed list pins one status per failure class; the broader
 * differential corpus is dns_ref.py's job (see run.sh).
 */
#include <stdio.h>
#include <string.h>

#include "dns.h"

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

static bool name_text_is(const AnemDnsName *name, const char *want)
{
    char buf[ANEM_DNS_MAX_TEXT];
    size_t len = 0;

    if (anem_dns_name_to_text(name, buf, sizeof buf, &len) != ANEM_DNS_OK) {
        return false;
    }
    (void)len;
    return strcmp(buf, want) == 0;
}

static bool name_roundtrips(const AnemDnsName *name)
{
    char text[ANEM_DNS_MAX_TEXT];
    AnemDnsName back;
    size_t len = 0;

    if (anem_dns_name_to_text(name, text, sizeof text, &len) != ANEM_DNS_OK) {
        return false;
    }
    if (anem_dns_name_from_text(text, &back) != ANEM_DNS_OK) {
        return false;
    }
    return back.len == name->len
        && memcmp(back.wire, name->wire, (size_t)name->len) == 0;
}

static bool msg_name_is(const AnemDnsMessage *msg, uint16_t off,
                        const char *want)
{
    AnemDnsName name;

    return anem_dns_name_at(msg, off, &name) == ANEM_DNS_OK
        && name_text_is(&name, want);
}

static const struct golden {
    const char *name;
    const char *hex;
} k_golden[] = {
    { "query_a",
      "123401000001000000000000"
      "03777777" "076578616d706c65" "03636f6d" "00" "0001" "0001" },
    { "cname_then_a",
      "abcd81800001000200000000"
      "03777777" "076578616d706c65" "03636f6d" "00" "0001" "0001"
      "c00c" "0005" "0001" "0000012c" "0008" "05616c696173" "c010"
      "c02d" "0001" "0001" "0000012c" "0004" "c0000201" },
    { "mx",
      "000181800001000100000000"
      "046d61696c" "076578616d706c65" "03636f6d" "00" "000f" "0001"
      "c00c" "000f" "0001" "00000e10" "0004" "000a" "c011" },
    { "soa_authority",
      "000281800001000000010000"
      "03777777" "076578616d706c65" "03636f6d" "00" "0001" "0001"
      "c00c" "0006" "0001" "0000012c" "0026"
      "026e73" "c010"
      "0a686f73746d6173746572" "c010"
      "00000001" "00000e10" "00000708" "00000e10" "0000012c" },
    { "root_ns", "000301000001000000000000" "00" "0002" "0001" },
    { "srv",
      "000481800001000100000000"
      "055f68656c70" "045f746370" "076578616d706c65" "03636f6d" "00"
      "0021" "0001"
      "c00c" "0021" "0001" "0000012c" "0008"
      "000a" "0014" "001f" "c017" },
    { "query_single_label",
      "beef01000001000000000000" "0161" "00" "0001" "0001" },
};

static const struct bad {
    const char *name;
    const char *hex;
    AnemDnsStatus want;
} k_bad[] = {
    { "empty", "", ANEM_DNS_SHORT },
    { "header_11", "0000000000000000000000", ANEM_DNS_SHORT },
    { "question_missing", "000101000001000000000000", ANEM_DNS_TRUNCATED },
    { "reserved_label_bits", "00010100000100000000000040",
      ANEM_DNS_BAD_LABEL },
    { "self_pointer", "000101000001000000000000c00c",
      ANEM_DNS_BAD_POINTER },
    { "forward_pointer", "000101000001000000000000c020",
      ANEM_DNS_BAD_POINTER },
    { "too_many_questions",
      "000101000005000000000000" "00000000", ANEM_DNS_TOO_MANY },
    { "trailing_byte",
      "0001010000010000000000000161000001000100", ANEM_DNS_TRAILING },
    { "rdlength_past_end",
      "00018180000100010000000001610000010001"
      "c00c000100010000000000ff", ANEM_DNS_TRUNCATED },
    { "cname_rdlength_short",
      "00018180000100010000000001610000010001"
      "c00c00050001000000000002016100", ANEM_DNS_BAD_RDLENGTH },
    { "question_type_missing",
      "0001010000010000000000000161000001", ANEM_DNS_SHORT },
};

static void check_specifics(const char *name, const AnemDnsMessage *msg)
{
    const AnemDnsRecord *rr;

    if (strcmp(name, "query_a") == 0) {
        check(msg->header.id == 0x1234u && msg->header.flags == 0x0100u,
              "query_a header");
        check(msg->header.qdcount == 1u && msg->header.ancount == 0u,
              "query_a counts");
        check(msg->questions[0].type == ANEM_DNS_TYPE_A
              && msg->questions[0].class_ == ANEM_DNS_CLASS_IN,
              "query_a type/class");
        check(msg_name_is(msg, msg->questions[0].name_off, "www.example.com"),
              "query_a name");
    } else if (strcmp(name, "cname_then_a") == 0) {
        AnemDnsName n;

        check(msg->header.ancount == 2u, "cname_then_a counts");
        rr = &msg->answers[0];
        check(rr->type == ANEM_DNS_TYPE_CNAME && rr->ttl == 300u,
              "cname_then_a first record");
        check(anem_dns_rdata_name(msg, rr, &n) == ANEM_DNS_OK
              && name_text_is(&n, "alias.example.com"),
              "cname_then_a rdata name");
        rr = &msg->answers[1];
        check(rr->type == ANEM_DNS_TYPE_A && rr->rdlength == 4u,
              "cname_then_a second record");
        check(rr->ttl == 300u, "cname_then_a ttl");
        check(msg_name_is(msg, rr->name_off, "alias.example.com"),
              "cname_then_a owner");
        check(anem_dns_rdata_name(msg, rr, &n) == ANEM_DNS_NO_NAME,
              "A record has no rdata name");
    } else if (strcmp(name, "mx") == 0) {
        AnemDnsName n;

        rr = &msg->answers[0];
        check(rr->type == ANEM_DNS_TYPE_MX, "mx type");
        check(msg->wire[rr->rdata_off] == 0x00u
              && msg->wire[rr->rdata_off + 1u] == 0x0au, "mx preference");
        check(anem_dns_rdata_name(msg, rr, &n) == ANEM_DNS_OK
              && name_text_is(&n, "example.com"), "mx exchange");
    } else if (strcmp(name, "soa_authority") == 0) {
        AnemDnsName n;

        check(msg->header.nscount == 1u, "soa counts");
        rr = &msg->authority[0];
        check(rr->type == ANEM_DNS_TYPE_SOA && rr->rdlength == 0x26u,
              "soa record");
        check(anem_dns_rdata_name(msg, rr, &n) == ANEM_DNS_OK
              && name_text_is(&n, "ns.example.com"), "soa mname");
    } else if (strcmp(name, "root_ns") == 0) {
        check(msg_name_is(msg, msg->questions[0].name_off, "."),
              "root name");
    } else if (strcmp(name, "srv") == 0) {
        AnemDnsName n;

        rr = &msg->answers[0];
        check(rr->type == ANEM_DNS_TYPE_SRV, "srv type");
        check(anem_dns_rdata_name(msg, rr, &n) == ANEM_DNS_OK
              && name_text_is(&n, "example.com"), "srv target");
    } else if (strcmp(name, "query_single_label") == 0) {
        check(msg_name_is(msg, msg->questions[0].name_off, "a"),
              "single-label name");
    }
}

static void check_golden(const struct golden *g, uint8_t *wire,
                         uint8_t *out)
{
    AnemDnsMessage msg;
    AnemDnsMessage again;
    size_t wire_len = 0;
    size_t out_len = 0;
    char label[160];

    (void)snprintf(label, sizeof label, "%s: hex", g->name);
    if (!hex_decode(g->hex, wire, 65535u, &wire_len)) {
        check(false, label);
        return;
    }
    check(true, label);

    (void)snprintf(label, sizeof label, "%s: parses", g->name);
    if (anem_dns_parse(wire, wire_len, &msg) != ANEM_DNS_OK) {
        check(false, label);
        return;
    }
    check(true, label);

    (void)snprintf(label, sizeof label, "%s: byte-identical write", g->name);
    if (anem_dns_write(&msg, out, 65535u, &out_len) != ANEM_DNS_OK) {
        check(false, label);
    } else {
        check(out_len == wire_len
              && memcmp(out, wire, wire_len) == 0, label);
    }

    (void)snprintf(label, sizeof label, "%s: reparse", g->name);
    if (anem_dns_parse(out, out_len, &again) != ANEM_DNS_OK) {
        check(false, label);
    } else {
        check(again.header.id == msg.header.id
              && again.header.flags == msg.header.flags
              && again.header.qdcount == msg.header.qdcount
              && again.header.ancount == msg.header.ancount
              && again.header.nscount == msg.header.nscount
              && again.header.arcount == msg.header.arcount, label);
    }

    for (unsigned i = 0; i < (unsigned)msg.header.qdcount; i++) {
        AnemDnsName n;

        (void)snprintf(label, sizeof label, "%s: question %u text", g->name, i);
        check(anem_dns_name_at(&msg, msg.questions[i].name_off, &n)
                  == ANEM_DNS_OK && name_roundtrips(&n), label);
    }
    for (unsigned i = 0; i < (unsigned)msg.header.ancount; i++) {
        AnemDnsName n;

        (void)snprintf(label, sizeof label, "%s: answer %u owner text",
                       g->name, i);
        check(anem_dns_name_at(&msg, msg.answers[i].name_off, &n)
                  == ANEM_DNS_OK && name_roundtrips(&n), label);
    }

    (void)snprintf(label, sizeof label, "%s: exact-fit write", g->name);
    check(anem_dns_write(&msg, out, wire_len, &out_len) == ANEM_DNS_OK, label);

    if (wire_len > 0u) {
        (void)snprintf(label, sizeof label, "%s: short buffer rejected",
                       g->name);
        check(anem_dns_write(&msg, out, wire_len - 1u, &out_len)
                  == ANEM_DNS_NOSPACE, label);
    }

    check_specifics(g->name, &msg);
}

static void check_bad(const struct bad *b, uint8_t *wire)
{
    AnemDnsMessage msg;
    size_t wire_len = 0;
    char label[160];

    (void)snprintf(label, sizeof label, "malformed %s: hex", b->name);
    if (!hex_decode(b->hex, wire, 65535u, &wire_len)) {
        check(false, label);
        return;
    }
    check(anem_dns_parse(wire, wire_len, &msg) == b->want, b->name);
}

static void check_name_over_255(uint8_t *wire)
{
    AnemDnsMessage msg;
    size_t off = 0;

    memset(wire, 0, 12);
    wire[5] = 1; /* qdcount = 1 */
    off = 12;
    for (unsigned i = 1; i <= 4; i++) {
        wire[off++] = 63;
        memset(wire + off, 'a', 63);
        off += 63;
    }
    wire[off++] = 0;
    check(anem_dns_parse(wire, off, &msg) == ANEM_DNS_BAD_LABEL,
          "malformed name_over_255");
}

static void check_text_conversions(void)
{
    static const uint8_t weird_wire[] = {
        1, 'a', 1, '.', 1, '\\', 1, 0x01, 0
    };
    AnemDnsName weird = { { 0 }, 0 };
    AnemDnsName n;
    char buf[ANEM_DNS_MAX_TEXT];
    size_t len = 0;
    uint8_t too_long[65];

    memcpy(weird.wire, weird_wire, sizeof weird_wire);
    weird.len = (uint16_t)sizeof weird_wire;
    check(name_roundtrips(&weird), "escaped name round-trips");
    check(anem_dns_name_to_text(&weird, buf, sizeof buf, &len) == ANEM_DNS_OK
          && len == 12u, "escaped name text length");

    check(anem_dns_name_from_text("www.example.com", &n) == ANEM_DNS_OK
          && n.len == 17u, "text to name");
    check(anem_dns_name_from_text("www.example.com.", &n) == ANEM_DNS_OK
          && n.len == 17u, "trailing dot accepted");
    check(anem_dns_name_from_text(".", &n) == ANEM_DNS_OK && n.len == 1u,
          "root from text");
    check(anem_dns_name_from_text("", &n) == ANEM_DNS_FORMAT,
          "empty name rejected");
    check(anem_dns_name_from_text(".a", &n) == ANEM_DNS_FORMAT,
          "leading dot rejected");
    check(anem_dns_name_from_text("a..b", &n) == ANEM_DNS_FORMAT,
          "empty label rejected");
    check(anem_dns_name_from_text("a\\", &n) == ANEM_DNS_FORMAT,
          "trailing escape rejected");
    check(anem_dns_name_from_text("\\256", &n) == ANEM_DNS_FORMAT,
          "escape over 255 rejected");

    memset(too_long, 'a', sizeof too_long);
    too_long[sizeof too_long - 1u] = '\0';
    check(anem_dns_name_from_text((const char *)too_long, &n)
              == ANEM_DNS_BAD_LABEL, "64-byte label rejected");

    check(anem_dns_name_to_text(&weird, buf, 4u, &len) == ANEM_DNS_NOSPACE,
          "text buffer too small");
}

static void check_build_query(uint8_t *out)
{
    AnemDnsName name;
    size_t len = 0;
    size_t want_len = 0;
    uint8_t want[64];

    if (anem_dns_name_from_text("www.example.com", &name) != ANEM_DNS_OK
        || !hex_decode("123401000001000000000000"
                       "03777777" "076578616d706c65" "03636f6d" "00"
                       "0001" "0001", want, sizeof want, &want_len)) {
        check(false, "build_query setup");
        return;
    }
    check(anem_dns_build_query(out, 65535u, 0x1234u, ANEM_DNS_FLAG_RD,
                               &name, ANEM_DNS_TYPE_A, ANEM_DNS_CLASS_IN,
                               &len) == ANEM_DNS_OK
          && len == want_len && memcmp(out, want, want_len) == 0,
          "build_query matches the hand-written vector");

    if (anem_dns_name_from_text(".", &name) == ANEM_DNS_OK
        && hex_decode("000301000001000000000000" "00" "0002" "0001",
                      want, sizeof want, &want_len)) {
        check(anem_dns_build_query(out, 65535u, 3u, ANEM_DNS_FLAG_RD,
                                   &name, ANEM_DNS_TYPE_NS,
                                   ANEM_DNS_CLASS_IN, &len) == ANEM_DNS_OK
              && len == want_len && memcmp(out, want, want_len) == 0,
              "build_query root name");
    } else {
        check(false, "build_query root setup");
    }

    check(anem_dns_build_query(out, 4u, 1u, 0, &name, 1u, 1u, &len)
              == ANEM_DNS_NOSPACE, "build_query short buffer");
}

static void check_contracts(uint8_t *wire, size_t wire_len)
{
    AnemDnsMessage msg;
    AnemDnsName name;
    uint8_t buf[64];
    char cbuf[64];
    size_t len = 0;

    memset(&msg, 0, sizeof msg);
    check(anem_dns_parse(NULL, wire_len, &msg) == ANEM_DNS_BADARG,
          "parse NULL wire");
    check(anem_dns_parse(wire, wire_len, NULL) == ANEM_DNS_BADARG,
          "parse NULL message");
    check(anem_dns_write(NULL, buf, sizeof buf, &len) == ANEM_DNS_BADARG,
          "write NULL message");
    check(anem_dns_write(&msg, NULL, sizeof buf, &len) == ANEM_DNS_BADARG,
          "write NULL buffer");
    check(anem_dns_write(&msg, buf, sizeof buf, NULL) == ANEM_DNS_BADARG,
          "write NULL length");
    check(anem_dns_name_at(NULL, 0, &name) == ANEM_DNS_BADARG,
          "name_at NULL message");
    check(anem_dns_name_to_text(NULL, cbuf, sizeof cbuf, &len)
              == ANEM_DNS_BADARG, "to_text NULL name");
    check(anem_dns_name_from_text(NULL, &name) == ANEM_DNS_BADARG,
          "from_text NULL text");
    check(anem_dns_name_from_text("a", NULL) == ANEM_DNS_BADARG,
          "from_text NULL output");

    if (anem_dns_parse(wire, wire_len, &msg) == ANEM_DNS_OK) {
        check(anem_dns_name_at(&msg, (uint16_t)wire_len, &name)
                  == ANEM_DNS_TRUNCATED, "name_at past end");
    } else {
        check(false, "contracts setup");
    }
}

int main(void)
{
    static uint8_t wire[65536];
    static uint8_t out[65536];
    static uint8_t huge[65537];
    AnemDnsMessage msg;

    for (size_t i = 0; i < sizeof k_golden / sizeof k_golden[0]; i++) {
        check_golden(&k_golden[i], wire, out);
    }
    for (size_t i = 0; i < sizeof k_bad / sizeof k_bad[0]; i++) {
        check_bad(&k_bad[i], wire);
    }
    check_name_over_255(wire);
    check_text_conversions();
    check_build_query(out);

    {
        size_t n = 0;

        (void)hex_decode(k_golden[0].hex, wire, sizeof wire, &n);
        check_contracts(wire, n);
        check(anem_dns_parse(huge, sizeof huge, &msg) == ANEM_DNS_TOO_LONG,
              "message over 65535 rejected");
    }

    if (failures != 0u) {
        printf("FAILED: %u check(s)\n", failures);
        return 1;
    }
    printf("all DNS vectors pass\n");
    return 0;
}
