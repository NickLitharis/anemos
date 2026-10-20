/* DNS wire codec — see dns.h for the contract. */
#include "dns.h"

/* No <string.h>: this unit must compile freestanding for ring 0, where the
 * CRT does not exist. These loops are the whole dependency. */
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

static bool bytes_equal(const uint8_t *a, const uint8_t *b, size_t n)
{
    for (size_t i = 0; i < n; i++) {
        if (a[i] != b[i]) {
            return false;
        }
    }
    return true;
}

/* ---- reading ---------------------------------------------------------- */

static uint16_t rd_u16(const uint8_t *p)
{
    return (uint16_t)(((unsigned)p[0] << 8) | (unsigned)p[1]);
}

static uint32_t rd_u32(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16)
         | ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

/* Decompress the name at `off`. On success *consumed is the offset just past
 * the name as stored at `off` (pointer included), which is what the caller
 * needs to continue parsing. `out` may be NULL for validation only.
 *
 * A pointer must point strictly before itself. That single rule makes the
 * loop provably terminating (RFC 1035 §4.1.4 requires a prior occurrence);
 * the hop counter is the belt to that braces. */
static AnemDnsStatus read_name(const uint8_t *wire, size_t wire_len,
                               size_t off, AnemDnsName *out, size_t *consumed)
{
    size_t pos = off;
    size_t end = 0;
    size_t out_len = 0;
    unsigned hops = 0;
    bool jumped = false;

    for (;;) {
        uint8_t b;

        if (pos >= wire_len) {
            return ANEM_DNS_TRUNCATED;
        }
        b = wire[pos];
        if (b == 0) {
            if (!jumped) {
                end = pos + 1u;
            }
            break;
        }
        if ((b & 0xc0u) == 0xc0u) {
            size_t target;

            if (pos + 1u >= wire_len) {
                return ANEM_DNS_TRUNCATED;
            }
            target = ((size_t)(b & 0x3fu) << 8) | (size_t)wire[pos + 1u];
            if (!jumped) {
                end = pos + 2u;
            }
            jumped = true;
#ifdef ANEM_DNS_NEGATIVE
            /* Negative control: accept any target, including forward and
             * self references. The malformed-input vectors must then fail. */
            if (target > wire_len) {
                return ANEM_DNS_BAD_POINTER;
            }
#else
            if (target >= pos) {
                return ANEM_DNS_BAD_POINTER;
            }
#endif
            hops++;
            if (hops > ANEM_DNS_MAX_POINTER_HOPS) {
                return ANEM_DNS_BAD_POINTER;
            }
            pos = target;
            continue;
        }
#ifndef ANEM_DNS_NEGATIVE
        if ((b & 0xc0u) != 0u) {
            return ANEM_DNS_BAD_LABEL;
        }
#endif
        if (pos + 1u + (size_t)b > wire_len) {
            return ANEM_DNS_TRUNCATED;
        }
        if (out_len + 1u + (size_t)b + 1u > ANEM_DNS_MAX_NAME) {
            return ANEM_DNS_BAD_LABEL;
        }
        if (out != NULL) {
            out->wire[out_len] = b;
            copy_bytes(out->wire + out_len + 1u, wire + pos + 1u, (size_t)b);
            out->len = (uint16_t)(out_len + 1u + (size_t)b);
        }
        out_len += 1u + (size_t)b;
        pos += 1u + (size_t)b;
    }
    if (out != NULL) {
        out->wire[out_len] = 0;
        out->len = (uint16_t)(out_len + 1u);
    }
    if (consumed != NULL) {
        *consumed = end;
    }
    return ANEM_DNS_OK;
}

/* RDATA consisting of `fixed` opaque bytes followed by exactly one name. */
static AnemDnsStatus check_one_name_rdata(const uint8_t *wire, size_t wire_len,
                                          size_t rdata_off, uint16_t rdlength,
                                          size_t fixed)
{
    AnemDnsStatus st;
    size_t consumed = 0;

    if (fixed > (size_t)rdlength) {
        return ANEM_DNS_BAD_RDLENGTH;
    }
    st = read_name(wire, wire_len, rdata_off + fixed, NULL, &consumed);
    if (st != ANEM_DNS_OK) {
        return st;
    }
    if (consumed != rdata_off + (size_t)rdlength) {
        return ANEM_DNS_BAD_RDLENGTH;
    }
    return ANEM_DNS_OK;
}

/* SOA RDATA: MNAME, RNAME, then five 32-bit fields (RFC 1035 §3.3.13). */
static AnemDnsStatus check_soa_rdata(const uint8_t *wire, size_t wire_len,
                                     size_t rdata_off, uint16_t rdlength)
{
    AnemDnsStatus st;
    size_t c1 = 0;
    size_t c2 = 0;

    st = read_name(wire, wire_len, rdata_off, NULL, &c1);
    if (st != ANEM_DNS_OK) {
        return st;
    }
    if (c1 - rdata_off > (size_t)rdlength) {
        return ANEM_DNS_BAD_RDLENGTH;
    }
    st = read_name(wire, wire_len, c1, NULL, &c2);
    if (st != ANEM_DNS_OK) {
        return st;
    }
    if (c2 - rdata_off > (size_t)rdlength) {
        return ANEM_DNS_BAD_RDLENGTH;
    }
    if ((size_t)rdlength - (c2 - rdata_off) != 20u) {
        return ANEM_DNS_BAD_RDLENGTH;
    }
    return ANEM_DNS_OK;
}

static AnemDnsStatus validate_rdata(const uint8_t *wire, size_t wire_len,
                                    const AnemDnsRecord *rr)
{
    switch (rr->type) {
    case ANEM_DNS_TYPE_NS:
    case ANEM_DNS_TYPE_CNAME:
    case ANEM_DNS_TYPE_PTR:
        return check_one_name_rdata(wire, wire_len, rr->rdata_off,
                                    rr->rdlength, 0);
    case ANEM_DNS_TYPE_MX:
        return check_one_name_rdata(wire, wire_len, rr->rdata_off,
                                    rr->rdlength, 2);
    case ANEM_DNS_TYPE_SRV:
        return check_one_name_rdata(wire, wire_len, rr->rdata_off,
                                    rr->rdlength, 6);
    case ANEM_DNS_TYPE_SOA:
        return check_soa_rdata(wire, wire_len, rr->rdata_off, rr->rdlength);
    default:
        return ANEM_DNS_OK; /* opaque */
    }
}

static AnemDnsStatus read_record(const uint8_t *wire, size_t wire_len,
                                 size_t *off, AnemDnsRecord *rec)
{
    AnemDnsStatus st;
    size_t consumed = 0;

    if (*off >= wire_len) {
        return ANEM_DNS_TRUNCATED;
    }
    rec->name_off = (uint16_t)*off;
    st = read_name(wire, wire_len, *off, NULL, &consumed);
    if (st != ANEM_DNS_OK) {
        return st;
    }
    *off = consumed;
    if (wire_len - *off < 10u) {
        return ANEM_DNS_SHORT;
    }
    rec->type = rd_u16(wire + *off);
    rec->class_ = rd_u16(wire + *off + 2u);
    rec->ttl = rd_u32(wire + *off + 4u);
    rec->rdlength = rd_u16(wire + *off + 8u);
    *off += 10u;
    rec->rdata_off = (uint16_t)*off;
    if ((size_t)rec->rdlength > wire_len - *off) {
        return ANEM_DNS_TRUNCATED;
    }
    st = validate_rdata(wire, wire_len, rec);
    if (st != ANEM_DNS_OK) {
        return st;
    }
    *off += (size_t)rec->rdlength;
    return ANEM_DNS_OK;
}

static AnemDnsStatus read_records(const uint8_t *wire, size_t wire_len,
                                  size_t *off, AnemDnsRecord *recs,
                                  unsigned count)
{
    for (unsigned i = 0; i < count; i++) {
        AnemDnsStatus st = read_record(wire, wire_len, off, &recs[i]);
        if (st != ANEM_DNS_OK) {
            return st;
        }
    }
    return ANEM_DNS_OK;
}

AnemDnsStatus anem_dns_parse(const void *wire_ptr, size_t wire_len,
                             AnemDnsMessage *out)
{
    const uint8_t *wire = wire_ptr;
    AnemDnsStatus st;
    size_t off = ANEM_DNS_HEADER_SIZE;

    if (wire == NULL || out == NULL) {
        return ANEM_DNS_BADARG;
    }
    if (wire_len > ANEM_DNS_MAX_WIRE) {
        return ANEM_DNS_TOO_LONG;
    }
    if (wire_len < ANEM_DNS_HEADER_SIZE) {
        return ANEM_DNS_SHORT;
    }
    zero_bytes(out, sizeof *out);
    out->wire = wire;
    out->wire_len = wire_len;
    out->header.id = rd_u16(wire);
    out->header.flags = rd_u16(wire + 2u);
    out->header.qdcount = rd_u16(wire + 4u);
    out->header.ancount = rd_u16(wire + 6u);
    out->header.nscount = rd_u16(wire + 8u);
    out->header.arcount = rd_u16(wire + 10u);

    if (out->header.qdcount > ANEM_DNS_MAX_QUESTIONS
        || out->header.ancount > ANEM_DNS_MAX_ANSWERS
        || out->header.nscount > ANEM_DNS_MAX_AUTHORITY
        || out->header.arcount > ANEM_DNS_MAX_ADDITIONAL) {
        return ANEM_DNS_TOO_MANY;
    }

    for (unsigned i = 0; i < (unsigned)out->header.qdcount; i++) {
        AnemDnsQuestion *q = &out->questions[i];
        size_t consumed = 0;

        if (off >= wire_len) {
            return ANEM_DNS_TRUNCATED;
        }
        q->name_off = (uint16_t)off;
        st = read_name(wire, wire_len, off, NULL, &consumed);
        if (st != ANEM_DNS_OK) {
            return st;
        }
        off = consumed;
        if (wire_len - off < 4u) {
            return ANEM_DNS_SHORT;
        }
        q->type = rd_u16(wire + off);
        q->class_ = rd_u16(wire + off + 2u);
        off += 4u;
    }

    st = read_records(wire, wire_len, &off, out->answers,
                      (unsigned)out->header.ancount);
    if (st != ANEM_DNS_OK) {
        return st;
    }
    st = read_records(wire, wire_len, &off, out->authority,
                      (unsigned)out->header.nscount);
    if (st != ANEM_DNS_OK) {
        return st;
    }
    st = read_records(wire, wire_len, &off, out->additional,
                      (unsigned)out->header.arcount);
    if (st != ANEM_DNS_OK) {
        return st;
    }
    if (off != wire_len) {
        return ANEM_DNS_TRAILING;
    }
    return ANEM_DNS_OK;
}

/* ---- writing ---------------------------------------------------------- */

typedef struct AnemDnsWriter {
    uint8_t *buf;
    size_t cap;
    size_t pos;
    bool overflow;
    uint16_t label_off[ANEM_DNS_MAX_COMPRESS];
    size_t n_labels;
} AnemDnsWriter;

static bool w_bytes(AnemDnsWriter *w, const void *src, size_t n)
{
    if (w->overflow) {
        return false;
    }
    if (n > w->cap - w->pos) {
        w->overflow = true;
        return false;
    }
    copy_bytes(w->buf + w->pos, src, n);
    w->pos += n;
    return true;
}

static bool w_u8(AnemDnsWriter *w, uint8_t v)
{
    return w_bytes(w, &v, 1u);
}

static bool w_u16(AnemDnsWriter *w, uint16_t v)
{
    uint8_t b[2];

    b[0] = (uint8_t)(v >> 8);
    b[1] = (uint8_t)(v & 0xffu);
    return w_bytes(w, b, sizeof b);
}

static bool w_u32(AnemDnsWriter *w, uint32_t v)
{
    uint8_t b[4];

    b[0] = (uint8_t)(v >> 24);
    b[1] = (uint8_t)((v >> 16) & 0xffu);
    b[2] = (uint8_t)((v >> 8) & 0xffu);
    b[3] = (uint8_t)(v & 0xffu);
    return w_bytes(w, b, sizeof b);
}

/* Does the name stored at `cand` in out[0..out_len) equal `name`? Both are
 * wire names; the stored one may itself use compression. */
static bool out_name_equals(const uint8_t *out, size_t out_len, size_t cand,
                            const uint8_t *name, size_t name_len)
{
    size_t p = cand;
    size_t i = 0;
    size_t total = 0;
    unsigned hops = 0;

    for (;;) {
        uint8_t b;

        if (p >= out_len || i >= name_len) {
            return false;
        }
        b = out[p];
        if ((b & 0xc0u) == 0xc0u) {
            size_t target;

            if (p + 1u >= out_len) {
                return false;
            }
            target = ((size_t)(b & 0x3fu) << 8) | (size_t)out[p + 1u];
            if (target >= p) {
                return false;
            }
            hops++;
            if (hops > ANEM_DNS_MAX_POINTER_HOPS) {
                return false;
            }
            p = target;
            continue;
        }
        if (b == 0) {
            return name[i] == 0 && i + 1u == name_len;
        }
        if ((b & 0xc0u) != 0u || b != name[i]) {
            return false;
        }
        if (p + 1u + (size_t)b > out_len || i + 1u + (size_t)b > name_len) {
            return false;
        }
        if (!bytes_equal(out + p + 1u, name + i + 1u, (size_t)b)) {
            return false;
        }
        total += (size_t)b + 1u;
        if (total + 1u > ANEM_DNS_MAX_NAME) {
            return false;
        }
        p += (size_t)b + 1u;
        i += (size_t)b + 1u;
    }
}

static AnemDnsStatus check_name(const AnemDnsName *name)
{
    size_t p = 0;

    if (name == NULL) {
        return ANEM_DNS_BADARG;
    }
    if (name->len == 0u || name->len > ANEM_DNS_MAX_NAME) {
        return ANEM_DNS_BAD_LABEL;
    }
    for (;;) {
        uint8_t b;

        if (p >= (size_t)name->len) {
            return ANEM_DNS_BAD_LABEL;
        }
        b = name->wire[p];
        if (b == 0) {
            return (p + 1u == (size_t)name->len) ? ANEM_DNS_OK
                                                 : ANEM_DNS_BAD_LABEL;
        }
        if ((b & 0xc0u) != 0u || b > ANEM_DNS_MAX_LABEL) {
            return ANEM_DNS_BAD_LABEL;
        }
        if (p + 1u + (size_t)b >= (size_t)name->len) {
            return ANEM_DNS_BAD_LABEL;
        }
        p += 1u + (size_t)b;
    }
}

static void w_label(AnemDnsWriter *w, const AnemDnsName *name, size_t p)
{
    size_t n = (size_t)name->wire[p] + 1u;

    if (!w->overflow && w->pos < 0x4000u
        && w->n_labels < ANEM_DNS_MAX_COMPRESS) {
        w->label_off[w->n_labels] = (uint16_t)w->pos;
        w->n_labels++;
    }
    (void)w_bytes(w, name->wire + p, n);
}

/* Emit `name`, using the longest suffix already present in the output.
 * The rule is deterministic (longest suffix, then lowest offset) so the
 * reference implementation in dns_ref.py can reproduce it byte for byte. */
static AnemDnsStatus w_name(AnemDnsWriter *w, const AnemDnsName *name)
{
    AnemDnsStatus st = check_name(name);
    size_t eligible;
    size_t best_s = 0;
    size_t best_c = 0;
    bool found = false;
    size_t p;

    if (st != ANEM_DNS_OK) {
        return st;
    }
    eligible = w->n_labels;
    for (size_t s = 0; s < (size_t)name->len; ) {
        size_t suffix_len = (size_t)name->len - s;

        if (suffix_len > 2u) {
            for (size_t k = 0; k < eligible; k++) {
                size_t c = (size_t)w->label_off[k];

                if (c >= 0x4000u) {
                    continue;
                }
                if (out_name_equals(w->buf, w->pos, c, name->wire + s,
                                    suffix_len)) {
                    if (!found || s < best_s || (s == best_s && c < best_c)) {
                        found = true;
                        best_s = s;
                        best_c = c;
                    }
                    break; /* offsets are recorded in emission order */
                }
            }
        }
        if (name->wire[s] == 0) {
            break;
        }
        s += 1u + (size_t)name->wire[s];
    }

    p = 0;
    if (found) {
        while (p < best_s) {
            w_label(w, name, p);
            p += 1u + (size_t)name->wire[p];
        }
        (void)w_u8(w, (uint8_t)(0xc0u | (best_c >> 8)));
        (void)w_u8(w, (uint8_t)(best_c & 0xffu));
    } else {
        while (name->wire[p] != 0) {
            w_label(w, name, p);
            p += 1u + (size_t)name->wire[p];
        }
        (void)w_u8(w, 0);
    }
    return w->overflow ? ANEM_DNS_NOSPACE : ANEM_DNS_OK;
}

static AnemDnsStatus read_source_name(const AnemDnsMessage *msg, size_t off,
                                      AnemDnsName *out, size_t *consumed)
{
    if (msg->wire == NULL || off >= msg->wire_len) {
        return ANEM_DNS_FORMAT;
    }
    return read_name(msg->wire, msg->wire_len, off, out, consumed);
}

static AnemDnsStatus w_question(AnemDnsWriter *w, const AnemDnsMessage *msg,
                                const AnemDnsQuestion *q)
{
    AnemDnsName name;
    AnemDnsStatus st;
    size_t consumed = 0;

    st = read_source_name(msg, q->name_off, &name, &consumed);
    if (st != ANEM_DNS_OK) {
        return st;
    }
    st = w_name(w, &name);
    if (st != ANEM_DNS_OK) {
        return st;
    }
    if (!w_u16(w, q->type) || !w_u16(w, q->class_)) {
        return ANEM_DNS_NOSPACE;
    }
    return ANEM_DNS_OK;
}

static AnemDnsStatus w_soa_rdata(AnemDnsWriter *w, const AnemDnsMessage *msg,
                                 const AnemDnsRecord *rr)
{
    AnemDnsName mname;
    AnemDnsName rname;
    AnemDnsStatus st;
    size_t c1 = 0;
    size_t c2 = 0;

    st = read_source_name(msg, rr->rdata_off, &mname, &c1);
    if (st != ANEM_DNS_OK) {
        return st;
    }
    st = read_source_name(msg, c1, &rname, &c2);
    if (st != ANEM_DNS_OK) {
        return st;
    }
    if (c2 > msg->wire_len || 20u > msg->wire_len - c2) {
        return ANEM_DNS_FORMAT;
    }
    if (c2 - (size_t)rr->rdata_off + 20u != (size_t)rr->rdlength) {
        return ANEM_DNS_FORMAT;
    }
    st = w_name(w, &mname);
    if (st != ANEM_DNS_OK) {
        return st;
    }
    st = w_name(w, &rname);
    if (st != ANEM_DNS_OK) {
        return st;
    }
    if (!w_bytes(w, msg->wire + c2, 20u)) {
        return ANEM_DNS_NOSPACE;
    }
    return ANEM_DNS_OK;
}

static AnemDnsStatus w_rdata(AnemDnsWriter *w, const AnemDnsMessage *msg,
                             const AnemDnsRecord *rr)
{
    AnemDnsName name;
    AnemDnsStatus st;
    size_t consumed = 0;
    size_t fixed;

    if ((size_t)rr->rdata_off > msg->wire_len
        || (size_t)rr->rdlength > msg->wire_len - (size_t)rr->rdata_off) {
        return ANEM_DNS_FORMAT;
    }
    switch (rr->type) {
    case ANEM_DNS_TYPE_NS:
    case ANEM_DNS_TYPE_CNAME:
    case ANEM_DNS_TYPE_PTR:
        fixed = 0;
        break;
    case ANEM_DNS_TYPE_MX:
        fixed = 2;
        break;
    case ANEM_DNS_TYPE_SRV:
        fixed = 6;
        break;
    case ANEM_DNS_TYPE_SOA:
        return w_soa_rdata(w, msg, rr);
    default:
        return w_bytes(w, msg->wire + rr->rdata_off, (size_t)rr->rdlength)
                   ? ANEM_DNS_OK : ANEM_DNS_NOSPACE;
    }
    if (fixed > (size_t)rr->rdlength) {
        return ANEM_DNS_FORMAT;
    }
    if (!w_bytes(w, msg->wire + rr->rdata_off, fixed)) {
        return ANEM_DNS_NOSPACE;
    }
    st = read_source_name(msg, (size_t)rr->rdata_off + fixed, &name, &consumed);
    if (st != ANEM_DNS_OK) {
        return st;
    }
    return w_name(w, &name);
}

static AnemDnsStatus w_record(AnemDnsWriter *w, const AnemDnsMessage *msg,
                              const AnemDnsRecord *rr)
{
    AnemDnsName owner;
    AnemDnsStatus st;
    size_t consumed = 0;
    size_t len_pos;
    size_t start;
    size_t rdlen;

    if ((size_t)rr->rdata_off > msg->wire_len
        || (size_t)rr->rdlength > msg->wire_len - (size_t)rr->rdata_off) {
        return ANEM_DNS_FORMAT;
    }
    st = read_source_name(msg, rr->name_off, &owner, &consumed);
    if (st != ANEM_DNS_OK) {
        return st;
    }
    st = w_name(w, &owner);
    if (st != ANEM_DNS_OK) {
        return st;
    }
    if (!w_u16(w, rr->type) || !w_u16(w, rr->class_) || !w_u32(w, rr->ttl)) {
        return ANEM_DNS_NOSPACE;
    }
    len_pos = w->pos;
    if (!w_u16(w, 0)) {
        return ANEM_DNS_NOSPACE;
    }
    start = w->pos;
    st = w_rdata(w, msg, rr);
    if (st != ANEM_DNS_OK) {
        return st;
    }
    if (w->overflow) {
        return ANEM_DNS_NOSPACE;
    }
    rdlen = w->pos - start;
    if (rdlen > 0xffffu || len_pos + 1u >= w->cap) {
        return ANEM_DNS_FORMAT;
    }
    w->buf[len_pos] = (uint8_t)(rdlen >> 8);
    w->buf[len_pos + 1u] = (uint8_t)(rdlen & 0xffu);
    return ANEM_DNS_OK;
}

AnemDnsStatus anem_dns_write(const AnemDnsMessage *msg, void *buf, size_t cap,
                             size_t *out_len)
{
    AnemDnsWriter w;
    AnemDnsStatus st;

    if (msg == NULL || buf == NULL || out_len == NULL) {
        return ANEM_DNS_BADARG;
    }
    if (msg->header.qdcount > ANEM_DNS_MAX_QUESTIONS
        || msg->header.ancount > ANEM_DNS_MAX_ANSWERS
        || msg->header.nscount > ANEM_DNS_MAX_AUTHORITY
        || msg->header.arcount > ANEM_DNS_MAX_ADDITIONAL) {
        return ANEM_DNS_BADARG;
    }
    if (msg->header.qdcount != 0 || msg->header.ancount != 0
        || msg->header.nscount != 0 || msg->header.arcount != 0) {
        if (msg->wire == NULL) {
            return ANEM_DNS_BADARG;
        }
    }

    zero_bytes(&w, sizeof w);
    w.buf = buf;
    w.cap = cap;
    (void)w_u16(&w, msg->header.id);
    (void)w_u16(&w, msg->header.flags);
    (void)w_u16(&w, msg->header.qdcount);
    (void)w_u16(&w, msg->header.ancount);
    (void)w_u16(&w, msg->header.nscount);
    (void)w_u16(&w, msg->header.arcount);

    for (unsigned i = 0; i < (unsigned)msg->header.qdcount; i++) {
        st = w_question(&w, msg, &msg->questions[i]);
        if (st != ANEM_DNS_OK) {
            return st;
        }
    }
    for (unsigned i = 0; i < (unsigned)msg->header.ancount; i++) {
        st = w_record(&w, msg, &msg->answers[i]);
        if (st != ANEM_DNS_OK) {
            return st;
        }
    }
    for (unsigned i = 0; i < (unsigned)msg->header.nscount; i++) {
        st = w_record(&w, msg, &msg->authority[i]);
        if (st != ANEM_DNS_OK) {
            return st;
        }
    }
    for (unsigned i = 0; i < (unsigned)msg->header.arcount; i++) {
        st = w_record(&w, msg, &msg->additional[i]);
        if (st != ANEM_DNS_OK) {
            return st;
        }
    }
    if (w.overflow) {
        return ANEM_DNS_NOSPACE;
    }
    *out_len = w.pos;
    return ANEM_DNS_OK;
}

AnemDnsStatus anem_dns_build_query(void *buf, size_t cap, uint16_t id,
                                   uint16_t flags, const AnemDnsName *qname,
                                   uint16_t qtype, uint16_t qclass,
                                   size_t *out_len)
{
    AnemDnsWriter w;
    AnemDnsStatus st;

    if (buf == NULL || qname == NULL || out_len == NULL) {
        return ANEM_DNS_BADARG;
    }
    zero_bytes(&w, sizeof w);
    w.buf = buf;
    w.cap = cap;
    (void)w_u16(&w, id);
    (void)w_u16(&w, flags);
    (void)w_u16(&w, 1);
    (void)w_u16(&w, 0);
    (void)w_u16(&w, 0);
    (void)w_u16(&w, 0);
    st = w_name(&w, qname);
    if (st != ANEM_DNS_OK) {
        return st;
    }
    if (!w_u16(&w, qtype) || !w_u16(&w, qclass) || w.overflow) {
        return ANEM_DNS_NOSPACE;
    }
    *out_len = w.pos;
    return ANEM_DNS_OK;
}

/* ---- accessors -------------------------------------------------------- */

AnemDnsStatus anem_dns_name_at(const AnemDnsMessage *msg, uint16_t off,
                               AnemDnsName *out)
{
    if (msg == NULL || msg->wire == NULL || out == NULL) {
        return ANEM_DNS_BADARG;
    }
    if ((size_t)off >= msg->wire_len) {
        return ANEM_DNS_TRUNCATED;
    }
    return read_name(msg->wire, msg->wire_len, off, out, NULL);
}

AnemDnsStatus anem_dns_rdata_name(const AnemDnsMessage *msg,
                                  const AnemDnsRecord *rr, AnemDnsName *out)
{
    size_t fixed;

    if (msg == NULL || msg->wire == NULL || rr == NULL || out == NULL) {
        return ANEM_DNS_BADARG;
    }
    switch (rr->type) {
    case ANEM_DNS_TYPE_NS:
    case ANEM_DNS_TYPE_CNAME:
    case ANEM_DNS_TYPE_PTR:
    case ANEM_DNS_TYPE_SOA: /* MNAME */
        fixed = 0;
        break;
    case ANEM_DNS_TYPE_MX:
        fixed = 2;
        break;
    case ANEM_DNS_TYPE_SRV:
        fixed = 6;
        break;
    default:
        return ANEM_DNS_NO_NAME;
    }
    if (fixed > (size_t)rr->rdlength
        || (size_t)rr->rdata_off > msg->wire_len
        || fixed > msg->wire_len - (size_t)rr->rdata_off) {
        return ANEM_DNS_BAD_RDLENGTH;
    }
    return read_name(msg->wire, msg->wire_len,
                     (size_t)rr->rdata_off + fixed, out, NULL);
}

/* ---- presentation form ------------------------------------------------ */

static bool text_put(char *buf, size_t cap, size_t *n, char c)
{
    if (*n + 1u >= cap) {
        return false;
    }
    buf[*n] = c;
    (*n)++;
    return true;
}

AnemDnsStatus anem_dns_name_to_text(const AnemDnsName *name, char *buf,
                                    size_t cap, size_t *out_len)
{
    AnemDnsStatus st;
    size_t p = 0;
    size_t n = 0;

    if (name == NULL || buf == NULL || out_len == NULL) {
        return ANEM_DNS_BADARG;
    }
    st = check_name(name);
    if (st != ANEM_DNS_OK) {
        return st;
    }
    if (name->len == 1u) {
        if (!text_put(buf, cap, &n, '.')) {
            return ANEM_DNS_NOSPACE;
        }
        buf[n] = '\0';
        *out_len = n;
        return ANEM_DNS_OK;
    }
    for (;;) {
        uint8_t b = name->wire[p];

        if (b == 0) {
            break;
        }
        if (p != 0 && !text_put(buf, cap, &n, '.')) {
            return ANEM_DNS_NOSPACE;
        }
        for (size_t i = 0; i < (size_t)b; i++) {
            uint8_t c = name->wire[p + 1u + i];

            if (c >= 0x21u && c <= 0x7eu && c != '.' && c != '\\') {
                if (!text_put(buf, cap, &n, (char)c)) {
                    return ANEM_DNS_NOSPACE;
                }
            } else if (c == '.' || c == '\\') {
                if (!text_put(buf, cap, &n, '\\')
                    || !text_put(buf, cap, &n, (char)c)) {
                    return ANEM_DNS_NOSPACE;
                }
            } else {
                if (!text_put(buf, cap, &n, '\\')
                    || !text_put(buf, cap, &n, (char)('0' + c / 100u))
                    || !text_put(buf, cap, &n, (char)('0' + (c / 10u) % 10u))
                    || !text_put(buf, cap, &n, (char)('0' + c % 10u))) {
                    return ANEM_DNS_NOSPACE;
                }
            }
        }
        p += 1u + (size_t)b;
    }
    buf[n] = '\0';
    *out_len = n;
    return ANEM_DNS_OK;
}

static bool is_digit(char c)
{
    return c >= '0' && c <= '9';
}

static AnemDnsStatus emit_text_label(uint8_t *wire, size_t *n,
                                     const uint8_t *label, size_t label_len)
{
    if (label_len == 0u || label_len > ANEM_DNS_MAX_LABEL) {
        return ANEM_DNS_FORMAT;
    }
    if (*n + 1u + label_len + 1u > ANEM_DNS_MAX_NAME) {
        return ANEM_DNS_BAD_LABEL;
    }
    wire[*n] = (uint8_t)label_len;
    (*n)++;
    copy_bytes(wire + *n, label, label_len);
    *n += label_len;
    return ANEM_DNS_OK;
}

AnemDnsStatus anem_dns_name_from_text(const char *text, AnemDnsName *out)
{
    uint8_t label[ANEM_DNS_MAX_LABEL];
    size_t label_len = 0;
    size_t n = 0;
    size_t len = 0;
    bool have_label = false;

    if (text == NULL || out == NULL) {
        return ANEM_DNS_BADARG;
    }
    while (len <= ANEM_DNS_MAX_TEXT && text[len] != '\0') {
        len++;
    }
    if (len > ANEM_DNS_MAX_TEXT || len == 0u) {
        return ANEM_DNS_FORMAT;
    }
    if (len == 1u && text[0] == '.') {
        out->wire[0] = 0;
        out->len = 1;
        return ANEM_DNS_OK;
    }
    for (size_t i = 0; i < len; i++) {
        char c = text[i];

        if (c == '.') {
            AnemDnsStatus st;

            if (!have_label) {
                return ANEM_DNS_FORMAT;
            }
            st = emit_text_label(out->wire, &n, label, label_len);
            if (st != ANEM_DNS_OK) {
                return st;
            }
            label_len = 0;
            have_label = false;
            continue;
        }
        if (c == '\\') {
            if (i + 1u >= len) {
                return ANEM_DNS_FORMAT;
            }
            if (i + 3u < len && is_digit(text[i + 1u])
                && is_digit(text[i + 2u]) && is_digit(text[i + 3u])) {
                unsigned v = (unsigned)(text[i + 1u] - '0') * 100u
                           + (unsigned)(text[i + 2u] - '0') * 10u
                           + (unsigned)(text[i + 3u] - '0');
                if (v > 255u) {
                    return ANEM_DNS_FORMAT;
                }
                if (label_len >= ANEM_DNS_MAX_LABEL) {
                    return ANEM_DNS_BAD_LABEL;
                }
                label[label_len] = (uint8_t)v;
                label_len++;
                i += 3u;
            } else {
                if (label_len >= ANEM_DNS_MAX_LABEL) {
                    return ANEM_DNS_BAD_LABEL;
                }
                label[label_len] = (uint8_t)text[i + 1u];
                label_len++;
                i += 1u;
            }
            have_label = true;
            continue;
        }
        if (label_len >= ANEM_DNS_MAX_LABEL) {
            return ANEM_DNS_BAD_LABEL;
        }
        label[label_len] = (uint8_t)c;
        label_len++;
        have_label = true;
    }
    if (have_label) {
        AnemDnsStatus st = emit_text_label(out->wire, &n, label, label_len);
        if (st != ANEM_DNS_OK) {
            return st;
        }
    }
    out->wire[n] = 0;
    out->len = (uint16_t)(n + 1u);
    return ANEM_DNS_OK;
}
