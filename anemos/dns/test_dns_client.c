/* Scenario suite for the DNS client.
 *
 * Responses are built byte by byte here, independently of the codec's writer,
 * so the client is not tested against its own serialiser.
 */
#include <stdio.h>
#include <string.h>

#include "dns_client.h"

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

static void wr16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)(v >> 8);
    p[1] = (uint8_t)(v & 0xffu);
}

static void wr32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v >> 24);
    p[1] = (uint8_t)((v >> 16) & 0xffu);
    p[2] = (uint8_t)((v >> 8) & 0xffu);
    p[3] = (uint8_t)(v & 0xffu);
}

static bool enc_name(const char *text, uint8_t *out, size_t cap, size_t *len)
{
    size_t n = 0;
    const char *p = text;

    if (strcmp(text, ".") == 0) {
        if (cap < 1u) {
            return false;
        }
        out[0] = 0;
        *len = 1;
        return true;
    }
    while (*p != '\0') {
        const char *dot = strchr(p, '.');
        size_t label = (dot != NULL) ? (size_t)(dot - p) : strlen(p);

        if (label == 0u || label > 63u || n + 1u + label + 1u > cap) {
            return false;
        }
        out[n] = (uint8_t)label;
        n++;
        memcpy(out + n, p, label);
        n += label;
        if (dot == NULL) {
            break;
        }
        p = dot + 1;
    }
    out[n] = 0;
    n++;
    *len = n;
    return true;
}

struct ans {
    const char *owner;
    uint16_t type;
    uint32_t ttl;
    const char *cname;
    const uint8_t *raw;
    uint16_t rawlen;
};

struct response {
    uint16_t id;
    uint16_t flags;
    const char *qname;
    uint16_t qtype;
    const struct ans *answers;
    unsigned count;
};

static bool build_response(const struct response *r, uint8_t *buf, size_t cap,
                           size_t *len)
{
    size_t n = 12;
    size_t l = 0;

    if (cap < 12u) {
        return false;
    }
    wr16(buf, r->id);
    wr16(buf + 2, r->flags);
    wr16(buf + 4, 1);
    wr16(buf + 6, (uint16_t)r->count);
    wr16(buf + 8, 0);
    wr16(buf + 10, 0);
    if (!enc_name(r->qname, buf + n, cap - n, &l)) {
        return false;
    }
    n += l;
    if (cap - n < 4u) {
        return false;
    }
    wr16(buf + n, r->qtype);
    wr16(buf + n + 2, ANEM_DNS_CLASS_IN);
    n += 4;
    for (unsigned i = 0; i < r->count; i++) {
        const struct ans *a = &r->answers[i];
        size_t start;
        size_t rdlen_at;

        if (!enc_name(a->owner, buf + n, cap - n, &l)) {
            return false;
        }
        n += l;
        if (cap - n < 10u) {
            return false;
        }
        wr16(buf + n, a->type);
        wr16(buf + n + 2, ANEM_DNS_CLASS_IN);
        wr32(buf + n + 4, a->ttl);
        rdlen_at = n + 8;
        n += 10;
        start = n;
        if (a->type == ANEM_DNS_TYPE_CNAME) {
            if (a->cname == NULL
                || !enc_name(a->cname, buf + n, cap - n, &l)) {
                return false;
            }
            n += l;
        } else {
            if (a->rawlen > cap - n) {
                return false;
            }
            memcpy(buf + n, a->raw, a->rawlen);
            n += a->rawlen;
        }
        wr16(buf + rdlen_at, (uint16_t)(n - start));
    }
    *len = n;
    return true;
}

static bool issue(AnemDnsClient *cl, const char *name_text, uint16_t qtype,
                  uint8_t *out, size_t *out_len)
{
    AnemDnsName name;

    if (anem_dns_name_from_text(name_text, &name) != ANEM_DNS_OK) {
        return false;
    }
    return anem_dns_client_query(cl, &name, qtype, ANEM_DNS_CLASS_IN, out,
                                 512u, out_len) == ANEM_DNS_CLIENT_OK;
}

static bool result_name_is(const AnemDnsResult *r, const char *want)
{
    char text[ANEM_DNS_MAX_TEXT];
    size_t len = 0;

    return anem_dns_name_to_text(&r->name, text, sizeof text, &len)
               == ANEM_DNS_OK
        && strcmp(text, want) == 0;
}

static void scenario_a(void)
{
    AnemDnsClient cl;
    uint8_t out[512];
    uint8_t resp[512];
    size_t out_len = 0;
    size_t resp_len = 0;
    static const uint8_t addr[4] = { 192, 0, 2, 1 };
    const struct ans answers[] = {
        { "www.example.com", ANEM_DNS_TYPE_A, 300u, NULL, addr, 4u }
    };
    struct response r;
    AnemDnsResult result;
    AnemDnsMessage query;
    AnemDnsName qname;

    memset(&r, 0, sizeof r);
    check(anem_dns_client_init(&cl, 1u) == ANEM_DNS_CLIENT_OK,
          "client init");
    check(issue(&cl, "www.example.com", ANEM_DNS_TYPE_A, out, &out_len),
          "query A builds");
    check(cl.pending && cl.qtype == ANEM_DNS_TYPE_A, "query is pending");
    check(anem_dns_parse(out, out_len, &query) == ANEM_DNS_OK
              && query.header.id == cl.id
              && (query.header.flags & ANEM_DNS_FLAG_RD) != 0u
              && query.header.qdcount == 1u, "query header");
    check(anem_dns_name_at(&query, query.questions[0].name_off, &qname)
              == ANEM_DNS_OK
          && anem_dns_name_to_text(&qname, (char *)resp, sizeof resp,
                                   &resp_len) == ANEM_DNS_OK,
          "query name");
    check(anem_dns_client_query(&cl, &qname, ANEM_DNS_TYPE_A,
                                ANEM_DNS_CLASS_IN, out, sizeof out, &out_len)
              == ANEM_DNS_CLIENT_BADSTATE, "second query is BADSTATE");

    r.id = cl.id;
    r.flags = 0x8180u; /* QR RD RA, NOERROR */
    r.qname = "www.example.com";
    r.qtype = ANEM_DNS_TYPE_A;
    r.answers = answers;
    r.count = 1;
    check(build_response(&r, resp, sizeof resp, &resp_len), "build A response");
    check(anem_dns_client_receive(&cl, resp, resp_len, &result)
              == ANEM_DNS_CLIENT_OK, "receive A response");
    check(result.family == 4u && result.addr[0] == 192u && result.addr[3] == 1u
          && result.ttl == 300u, "A answer fields");
    check(result_name_is(&result, "www.example.com"), "A answer name");
    check(!cl.pending, "transaction consumed");
    check(anem_dns_client_receive(&cl, resp, resp_len, &result)
              == ANEM_DNS_CLIENT_BADSTATE, "receive without pending");
}

static void scenario_aaaa_and_case(void)
{
    AnemDnsClient cl;
    uint8_t out[512];
    uint8_t resp[512];
    size_t out_len = 0;
    size_t resp_len = 0;
    static const uint8_t addr[16] = {
        0x20, 0x01, 0x0d, 0xb8, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1
    };
    const struct ans answers[] = {
        { "WWW.Example.COM", ANEM_DNS_TYPE_AAAA, 60u, NULL, addr, 16u }
    };
    struct response r;
    AnemDnsResult result;

    (void)anem_dns_client_init(&cl, 2u);
    check(issue(&cl, "www.example.com", ANEM_DNS_TYPE_AAAA, out, &out_len),
          "query AAAA builds");
    r.id = cl.id;
    r.flags = 0x8180u;
    r.qname = "WWW.EXAMPLE.COM";
    r.qtype = ANEM_DNS_TYPE_AAAA;
    r.answers = answers;
    r.count = 1;
    check(build_response(&r, resp, sizeof resp, &resp_len), "build AAAA response");
    check(anem_dns_client_receive(&cl, resp, resp_len, &result)
              == ANEM_DNS_CLIENT_OK, "receive AAAA response");
    check(result.family == 6u && result.addr[0] == 0x20u
          && result.addr[15] == 1u, "AAAA fields");
    check(result_name_is(&result, "www.example.com"),
          "question match is case-insensitive");
}

static void scenario_cname(void)
{
    AnemDnsClient cl;
    uint8_t out[512];
    uint8_t resp[512];
    size_t out_len = 0;
    size_t resp_len = 0;
    static const uint8_t addr[4] = { 192, 0, 2, 9 };
    AnemDnsResult result;

    {
        const struct ans answers[] = {
            { "www.example.com", ANEM_DNS_TYPE_CNAME, 60u,
              "alias.example.com", NULL, 0u },
            { "alias.example.com", ANEM_DNS_TYPE_A, 120u, NULL, addr, 4u }
        };
        struct response r;

        memset(&r, 0, sizeof r);
        r.id = 0;
        r.flags = 0x8180u;
        r.qname = "www.example.com";
        r.qtype = ANEM_DNS_TYPE_A;
        r.answers = answers;
        r.count = 2;
        (void)anem_dns_client_init(&cl, 3u);
        check(issue(&cl, "www.example.com", ANEM_DNS_TYPE_A, out, &out_len),
              "CNAME query builds");
        r.id = cl.id;
        check(build_response(&r, resp, sizeof resp, &resp_len),
              "build CNAME chain");
        check(anem_dns_client_receive(&cl, resp, resp_len, &result)
                  == ANEM_DNS_CLIENT_OK, "CNAME chain resolves");
        check(result.family == 4u && result.addr[3] == 9u
              && result.ttl == 120u, "CNAME chain address");
        check(result_name_is(&result, "alias.example.com"),
              "CNAME chain final name");
    }
    {
        const struct ans answers[] = {
            { "www.example.com", ANEM_DNS_TYPE_CNAME, 60u,
              "alias.example.com", NULL, 0u }
        };
        struct response r;

        memset(&r, 0, sizeof r);
        r.flags = 0x8180u;
        r.qname = "www.example.com";
        r.qtype = ANEM_DNS_TYPE_A;
        r.answers = answers;
        r.count = 1;
        (void)anem_dns_client_init(&cl, 4u);
        check(issue(&cl, "www.example.com", ANEM_DNS_TYPE_A, out, &out_len),
              "CNAME-only query builds");
        r.id = cl.id;
        check(build_response(&r, resp, sizeof resp, &resp_len),
              "build CNAME-only response");
        check(anem_dns_client_receive(&cl, resp, resp_len, &result)
                  == ANEM_DNS_CLIENT_CNAME, "chain ending in CNAME");
        check(result_name_is(&result, "alias.example.com"),
              "CNAME target reported");
    }
}

static void scenario_loop(void)
{
    AnemDnsClient cl;
    uint8_t out[512];
    uint8_t resp[512];
    size_t out_len = 0;
    size_t resp_len = 0;
    const struct ans answers[] = {
        { "a.example.com", ANEM_DNS_TYPE_CNAME, 60u, "b.example.com", NULL, 0u },
        { "b.example.com", ANEM_DNS_TYPE_CNAME, 60u, "a.example.com", NULL, 0u }
    };
    struct response r;
    AnemDnsResult result;

    memset(&r, 0, sizeof r);
    r.flags = 0x8180u;
    r.qname = "a.example.com";
    r.qtype = ANEM_DNS_TYPE_A;
    r.answers = answers;
    r.count = 2;
    (void)anem_dns_client_init(&cl, 5u);
    check(issue(&cl, "a.example.com", ANEM_DNS_TYPE_A, out, &out_len),
          "loop query builds");
    r.id = cl.id;
    check(build_response(&r, resp, sizeof resp, &resp_len), "build loop response");
    check(anem_dns_client_receive(&cl, resp, resp_len, &result)
              == ANEM_DNS_CLIENT_LOOP, "CNAME cycle is LOOP");
    check(!cl.pending, "loop consumes the transaction");
}

static void scenario_rcodes(void)
{
    AnemDnsClient cl;
    uint8_t out[512];
    uint8_t resp[512];
    size_t out_len = 0;
    size_t resp_len = 0;
    struct response r;
    AnemDnsResult result;

    memset(&r, 0, sizeof r);
    r.qname = "www.example.com";
    r.qtype = ANEM_DNS_TYPE_A;
    r.answers = NULL;
    r.count = 0;

    (void)anem_dns_client_init(&cl, 6u);
    (void)issue(&cl, "www.example.com", ANEM_DNS_TYPE_A, out, &out_len);
    r.id = cl.id;
    r.flags = 0x8183u; /* NXDOMAIN */
    check(build_response(&r, resp, sizeof resp, &resp_len), "build NXDOMAIN");
    check(anem_dns_client_receive(&cl, resp, resp_len, &result)
              == ANEM_DNS_CLIENT_RCODE, "NXDOMAIN is RCODE");
    check(result.rcode == 3u, "NXDOMAIN code");

    (void)issue(&cl, "www.example.com", ANEM_DNS_TYPE_A, out, &out_len);
    r.id = cl.id;
    r.flags = 0x8182u; /* SERVFAIL */
    check(build_response(&r, resp, sizeof resp, &resp_len), "build SERVFAIL");
    check(anem_dns_client_receive(&cl, resp, resp_len, &result)
              == ANEM_DNS_CLIENT_RCODE && result.rcode == 2u,
          "SERVFAIL code");

    (void)issue(&cl, "www.example.com", ANEM_DNS_TYPE_A, out, &out_len);
    r.id = cl.id;
    r.flags = 0x8380u; /* TC */
    check(build_response(&r, resp, sizeof resp, &resp_len), "build TC");
    check(anem_dns_client_receive(&cl, resp, resp_len, &result)
              == ANEM_DNS_CLIENT_TRUNCATED, "TC is TRUNCATED");

    (void)issue(&cl, "www.example.com", ANEM_DNS_TYPE_A, out, &out_len);
    r.id = cl.id;
    r.flags = 0x8180u;
    check(build_response(&r, resp, sizeof resp, &resp_len), "build empty answer");
    check(anem_dns_client_receive(&cl, resp, resp_len, &result)
              == ANEM_DNS_CLIENT_NO_ANSWER, "no data is NO_ANSWER");
}

static void scenario_ignored_and_malformed(void)
{
    AnemDnsClient cl;
    uint8_t out[512];
    uint8_t resp[512];
    size_t out_len = 0;
    size_t resp_len = 0;
    static const uint8_t addr[4] = { 192, 0, 2, 1 };
    const struct ans answers[] = {
        { "www.example.com", ANEM_DNS_TYPE_A, 300u, NULL, addr, 4u }
    };
    struct response r;
    AnemDnsResult result;

    memset(&r, 0, sizeof r);
    r.qname = "www.example.com";
    r.qtype = ANEM_DNS_TYPE_A;
    r.answers = answers;
    r.count = 1;
    r.flags = 0x8180u;

    (void)anem_dns_client_init(&cl, 7u);
    (void)issue(&cl, "www.example.com", ANEM_DNS_TYPE_A, out, &out_len);
    r.id = (uint16_t)(cl.id + 1u);
    check(build_response(&r, resp, sizeof resp, &resp_len), "build wrong id");
    check(anem_dns_client_receive(&cl, resp, resp_len, &result)
              == ANEM_DNS_CLIENT_IGNORED, "wrong id is ignored");
    check(cl.pending, "ignored frame leaves the query pending");

    r.id = cl.id;
    r.qname = "other.example.com";
    check(build_response(&r, resp, sizeof resp, &resp_len), "build wrong question");
    check(anem_dns_client_receive(&cl, resp, resp_len, &result)
              == ANEM_DNS_CLIENT_IGNORED, "wrong question is ignored");

    r.qname = "www.example.com";
    r.flags = 0x0180u; /* QR clear */
    check(build_response(&r, resp, sizeof resp, &resp_len), "build non-response");
    check(anem_dns_client_receive(&cl, resp, resp_len, &result)
              == ANEM_DNS_CLIENT_IGNORED, "query frame is ignored");

    check(anem_dns_client_receive(&cl, out, 5u, &result)
              == ANEM_DNS_CLIENT_MALFORMED, "short frame is MALFORMED");
    check(result.codec_status == (int)ANEM_DNS_SHORT, "codec status reported");
    check(cl.pending, "malformed frame leaves the query pending");

    r.flags = 0x8180u;
    check(build_response(&r, resp, sizeof resp, &resp_len), "build valid response");
    check(anem_dns_client_receive(&cl, resp, resp_len, &result)
              == ANEM_DNS_CLIENT_OK, "transaction survives a stray frame");
}

static void scenario_contracts(void)
{
    AnemDnsClient cl;
    AnemDnsName name;
    uint8_t out[512];
    size_t len = 0;
    AnemDnsResult result;

    check(anem_dns_client_init(NULL, 1u) == ANEM_DNS_CLIENT_BADARG,
          "init NULL client");
    check(anem_dns_client_init(&cl, 1u) == ANEM_DNS_CLIENT_OK, "init for contracts");
    check(anem_dns_name_from_text("www.example.com", &name) == ANEM_DNS_OK,
          "contracts name");
    check(anem_dns_client_query(NULL, &name, ANEM_DNS_TYPE_A,
                                ANEM_DNS_CLASS_IN, out, sizeof out, &len)
              == ANEM_DNS_CLIENT_BADARG, "query NULL client");
    check(anem_dns_client_query(&cl, NULL, ANEM_DNS_TYPE_A,
                                ANEM_DNS_CLASS_IN, out, sizeof out, &len)
              == ANEM_DNS_CLIENT_BADARG, "query NULL name");
    check(anem_dns_client_query(&cl, &name, ANEM_DNS_TYPE_MX,
                                ANEM_DNS_CLASS_IN, out, sizeof out, &len)
              == ANEM_DNS_CLIENT_BADARG, "query unsupported type");
    check(anem_dns_client_query(&cl, &name, ANEM_DNS_TYPE_A,
                                ANEM_DNS_CLASS_IN, out, 4u, &len)
              == ANEM_DNS_CLIENT_NOSPACE, "query short buffer");
    check(!cl.pending, "failed query is not pending");
    check(anem_dns_client_receive(NULL, out, sizeof out, &result)
              == ANEM_DNS_CLIENT_BADARG, "receive NULL client");
    check(anem_dns_client_receive(&cl, NULL, 0, &result)
              == ANEM_DNS_CLIENT_BADARG, "receive NULL wire");
    check(anem_dns_client_receive(&cl, out, sizeof out, NULL)
              == ANEM_DNS_CLIENT_BADARG, "receive NULL result");
}

int main(void)
{
    scenario_a();
    scenario_aaaa_and_case();
    scenario_cname();
    scenario_loop();
    scenario_rcodes();
    scenario_ignored_and_malformed();
    scenario_contracts();

    if (failures != 0u) {
        printf("FAILED: %u check(s)\n", failures);
        return 1;
    }
    printf("all DNS client scenarios pass\n");
    return 0;
}
