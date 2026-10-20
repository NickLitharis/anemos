/* DNS client — see dns_client.h for the contract. */
#include "dns_client.h"

static void copy_bytes(void *dst, const void *src, size_t n)
{
    uint8_t *d = dst;
    const uint8_t *s = src;

    for (size_t i = 0; i < n; i++) {
        d[i] = s[i];
    }
}

static void zero_bytes(void *dst, size_t n)
{
    uint8_t *d = dst;

    for (size_t i = 0; i < n; i++) {
        d[i] = 0;
    }
}

static uint8_t fold_ascii(uint8_t c)
{
#ifdef ANEM_DNS_CLIENT_NEGATIVE
    /* Negative control: case-sensitive comparison. The case-echo vector and
     * the corpus must reject it. */
    return c;
#else
    return (c >= 'A' && c <= 'Z') ? (uint8_t)(c + 32u) : c;
#endif
}

/* RFC 1035 §2.3.3: names compare case-insensitively for ASCII letters. */
static bool name_equal_ci(const AnemDnsName *a, const AnemDnsName *b)
{
    if (a->len != b->len) {
        return false;
    }
    for (size_t i = 0; i < (size_t)a->len; i++) {
        if (fold_ascii(a->wire[i]) != fold_ascii(b->wire[i])) {
            return false;
        }
    }
    return true;
}

static uint16_t next_id(AnemDnsClient *cl)
{
    cl->seed = cl->seed * 1664525u + 1013904223u;
    return (uint16_t)(cl->seed & 0xffffu);
}

AnemDnsClientStatus anem_dns_client_init(AnemDnsClient *cl, uint32_t seed)
{
    if (cl == NULL) {
        return ANEM_DNS_CLIENT_BADARG;
    }
    zero_bytes(cl, sizeof *cl);
    cl->seed = seed;
    return ANEM_DNS_CLIENT_OK;
}

AnemDnsClientStatus anem_dns_client_query(AnemDnsClient *cl,
                                          const AnemDnsName *name,
                                          uint16_t qtype, uint16_t qclass,
                                          void *out, size_t cap,
                                          size_t *out_len)
{
    AnemDnsStatus st;

    if (cl == NULL || name == NULL || out == NULL || out_len == NULL) {
        return ANEM_DNS_CLIENT_BADARG;
    }
    if (qtype != ANEM_DNS_TYPE_A && qtype != ANEM_DNS_TYPE_AAAA) {
        return ANEM_DNS_CLIENT_BADARG;
    }
    if (cl->pending) {
        return ANEM_DNS_CLIENT_BADSTATE;
    }
    cl->id = next_id(cl);
    st = anem_dns_build_query(out, cap, cl->id, ANEM_DNS_FLAG_RD, name, qtype,
                              qclass, out_len);
    if (st == ANEM_DNS_NOSPACE) {
        return ANEM_DNS_CLIENT_NOSPACE;
    }
    if (st != ANEM_DNS_OK) {
        return ANEM_DNS_CLIENT_BADARG;
    }
    cl->qname = *name;
    cl->qtype = qtype;
    cl->qclass = qclass;
    cl->pending = true;
    return ANEM_DNS_CLIENT_OK;
}

/* Follow CNAMEs from the question name and return the first A/AAAA for the
 * final name. The walk is bounded by the answer count, so a cycle returns
 * LOOP instead of hanging. */
static AnemDnsClientStatus walk_answers(const AnemDnsMessage *msg,
                                        const AnemDnsClient *cl,
                                        AnemDnsResult *result)
{
    AnemDnsName current = cl->qname;
    size_t need = (cl->qtype == ANEM_DNS_TYPE_A) ? 4u : 16u;

    for (unsigned step = 0;; step++) {
        bool followed = false;

        for (unsigned i = 0; i < (unsigned)msg->header.ancount; i++) {
            const AnemDnsRecord *rr = &msg->answers[i];
            AnemDnsName owner;
            AnemDnsName target;

            if (rr->type != ANEM_DNS_TYPE_CNAME) {
                continue;
            }
            if (anem_dns_name_at(msg, rr->name_off, &owner) != ANEM_DNS_OK
                || !name_equal_ci(&owner, &current)) {
                continue;
            }
            if (anem_dns_rdata_name(msg, rr, &target) != ANEM_DNS_OK) {
                continue;
            }
            current = target;
            followed = true;
            break;
        }
        if (followed) {
            if (step >= (unsigned)msg->header.ancount) {
                result->name = current;
                return ANEM_DNS_CLIENT_LOOP;
            }
            continue;
        }

        for (unsigned i = 0; i < (unsigned)msg->header.ancount; i++) {
            const AnemDnsRecord *rr = &msg->answers[i];
            AnemDnsName owner;

            if (rr->type != cl->qtype || (size_t)rr->rdlength != need) {
                continue;
            }
            if (anem_dns_name_at(msg, rr->name_off, &owner) != ANEM_DNS_OK
                || !name_equal_ci(&owner, &current)) {
                continue;
            }
            if ((size_t)rr->rdata_off > msg->wire_len
                || need > msg->wire_len - (size_t)rr->rdata_off) {
                continue;
            }
            copy_bytes(result->addr, msg->wire + rr->rdata_off, need);
            result->family = (need == 4u) ? 4u : 6u;
            result->ttl = rr->ttl;
            result->name = current;
            return ANEM_DNS_CLIENT_OK;
        }
        break;
    }

    result->name = current;
    if (!name_equal_ci(&current, &cl->qname)) {
        return ANEM_DNS_CLIENT_CNAME;
    }
    return ANEM_DNS_CLIENT_NO_ANSWER;
}

AnemDnsClientStatus anem_dns_client_receive(AnemDnsClient *cl,
                                            const void *wire, size_t wire_len,
                                            AnemDnsResult *result)
{
    AnemDnsMessage msg;
    AnemDnsName qname;
    AnemDnsStatus st;
    uint16_t rcode;

    if (cl == NULL || wire == NULL || result == NULL) {
        return ANEM_DNS_CLIENT_BADARG;
    }
    zero_bytes(result, sizeof *result);
    if (!cl->pending) {
        return ANEM_DNS_CLIENT_BADSTATE;
    }
    st = anem_dns_parse(wire, wire_len, &msg);
    if (st != ANEM_DNS_OK) {
        result->codec_status = (int)st;
        return ANEM_DNS_CLIENT_MALFORMED;
    }
    if ((msg.header.flags & ANEM_DNS_FLAG_QR) == 0u
        || msg.header.id != cl->id || msg.header.qdcount != 1u) {
        return ANEM_DNS_CLIENT_IGNORED;
    }
    if (anem_dns_name_at(&msg, msg.questions[0].name_off, &qname)
            != ANEM_DNS_OK
        || !name_equal_ci(&qname, &cl->qname)
        || msg.questions[0].type != cl->qtype
        || msg.questions[0].class_ != cl->qclass) {
        return ANEM_DNS_CLIENT_IGNORED;
    }

    cl->pending = false;
    if ((msg.header.flags & ANEM_DNS_FLAG_TC) != 0u) {
        return ANEM_DNS_CLIENT_TRUNCATED;
    }
    rcode = (uint16_t)(msg.header.flags & 0x000fu);
    if (rcode != 0u) {
        result->rcode = rcode;
        return ANEM_DNS_CLIENT_RCODE;
    }
    return walk_answers(&msg, cl, result);
}
