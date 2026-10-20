/* DHCP wire codec — see dhcp.h for the contract. */
#include "dhcp.h"

/* No <string.h>: freestanding discipline, same as the DNS unit. */
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

static uint16_t rd_u16(const uint8_t *p)
{
    return (uint16_t)(((unsigned)p[0] << 8) | (unsigned)p[1]);
}

static uint32_t rd_u32(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16)
         | ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

static void parse_header(const uint8_t *wire, AnemDhcpHeader *hdr)
{
    hdr->op = wire[0];
    hdr->htype = wire[1];
    hdr->hlen = wire[2];
    hdr->hops = wire[3];
    hdr->xid = rd_u32(wire + 4u);
    hdr->secs = rd_u16(wire + 8u);
    hdr->flags = rd_u16(wire + 10u);
    hdr->ciaddr = rd_u32(wire + 12u);
    hdr->yiaddr = rd_u32(wire + 16u);
    hdr->siaddr = rd_u32(wire + 20u);
    hdr->giaddr = rd_u32(wire + 24u);
    copy_bytes(hdr->chaddr, wire + 28u, ANEM_DHCP_CHADDR_SIZE);
    copy_bytes(hdr->sname, wire + 44u, ANEM_DHCP_SNAME_SIZE);
    copy_bytes(hdr->file, wire + 108u, ANEM_DHCP_FILE_SIZE);
}

AnemDhcpStatus anem_dhcp_parse(const void *wire_ptr, size_t wire_len,
                               AnemDhcpMessage *out)
{
    const uint8_t *wire = wire_ptr;
    size_t pos;

    if (wire == NULL || out == NULL) {
        return ANEM_DHCP_BADARG;
    }
    if (wire_len > ANEM_DHCP_MAX_WIRE) {
        return ANEM_DHCP_TOO_LONG;
    }
    if (wire_len < ANEM_DHCP_MIN_SIZE) {
        return ANEM_DHCP_SHORT;
    }
    zero_bytes(out, sizeof *out);
    out->wire = wire;
    out->wire_len = wire_len;
    parse_header(wire, &out->header);
    if (out->header.hlen > ANEM_DHCP_CHADDR_SIZE) {
        return ANEM_DHCP_BAD_HLEN;
    }
    if (rd_u32(wire + ANEM_DHCP_FIXED_SIZE) != ANEM_DHCP_MAGIC_COOKIE) {
        return ANEM_DHCP_BAD_COOKIE;
    }

    pos = ANEM_DHCP_MIN_SIZE;
    for (;;) {
        uint8_t b;
        uint8_t len;
        AnemDhcpOption *opt;

        if (pos >= wire_len) {
            return ANEM_DHCP_NO_END;
        }
        b = wire[pos];
        if (b == ANEM_DHCP_OPT_END) {
            pos++;
            break;
        }
        if (out->option_count >= ANEM_DHCP_MAX_OPTIONS) {
            return ANEM_DHCP_TOO_MANY;
        }
        opt = &out->options[out->option_count];
        opt->off = (uint16_t)pos;
        opt->code = b;
        if (b == ANEM_DHCP_OPT_PAD) {
            opt->len = 0;
            out->option_count++;
            pos++;
            continue;
        }
        if (pos + 1u >= wire_len) {
            return ANEM_DHCP_BAD_OPTION;
        }
        len = wire[pos + 1u];
#ifndef ANEM_DHCP_NEGATIVE
        /* Negative control: skip the bound so a truncated option is accepted
         * and the malformed vectors must disagree. */
        if ((size_t)len > wire_len - (pos + 2u)) {
            return ANEM_DHCP_BAD_OPTION;
        }
#endif
        opt->len = len;
        out->option_count++;
        pos += 2u + (size_t)len;
    }

    {
        size_t pad = 0;

        while (pos < wire_len) {
            if (wire[pos] != 0u) {
                return ANEM_DHCP_TRAILING;
            }
            pos++;
            pad++;
        }
        out->pad_len = (uint16_t)pad;
    }
    return ANEM_DHCP_OK;
}

/* ---- writing ---------------------------------------------------------- */

typedef struct DhcpWriterState {
    uint8_t *buf;
    size_t cap;
    size_t pos;
    bool overflow;
} DhcpWriterState;

static void put_bytes(DhcpWriterState *w, const void *src, size_t n)
{
    if (w->overflow) {
        return;
    }
    if (n > w->cap - w->pos) {
        w->overflow = true;
        return;
    }
    copy_bytes(w->buf + w->pos, src, n);
    w->pos += n;
}

static void put_u8(DhcpWriterState *w, uint8_t v)
{
    put_bytes(w, &v, 1u);
}

static void put_u16(DhcpWriterState *w, uint16_t v)
{
    uint8_t b[2];

    b[0] = (uint8_t)(v >> 8);
    b[1] = (uint8_t)(v & 0xffu);
    put_bytes(w, b, sizeof b);
}

static void put_u32(DhcpWriterState *w, uint32_t v)
{
    uint8_t b[4];

    b[0] = (uint8_t)(v >> 24);
    b[1] = (uint8_t)((v >> 16) & 0xffu);
    b[2] = (uint8_t)((v >> 8) & 0xffu);
    b[3] = (uint8_t)(v & 0xffu);
    put_bytes(w, b, sizeof b);
}

static void put_header(DhcpWriterState *w, const AnemDhcpHeader *hdr)
{
    put_u8(w, hdr->op);
    put_u8(w, hdr->htype);
    put_u8(w, hdr->hlen);
    put_u8(w, hdr->hops);
    put_u32(w, hdr->xid);
    put_u16(w, hdr->secs);
    put_u16(w, hdr->flags);
    put_u32(w, hdr->ciaddr);
    put_u32(w, hdr->yiaddr);
    put_u32(w, hdr->siaddr);
    put_u32(w, hdr->giaddr);
    put_bytes(w, hdr->chaddr, ANEM_DHCP_CHADDR_SIZE);
    put_bytes(w, hdr->sname, ANEM_DHCP_SNAME_SIZE);
    put_bytes(w, hdr->file, ANEM_DHCP_FILE_SIZE);
    put_u32(w, ANEM_DHCP_MAGIC_COOKIE);
}

AnemDhcpStatus anem_dhcp_write(const AnemDhcpMessage *msg, void *buf,
                               size_t cap, size_t *out_len)
{
    DhcpWriterState w;

    if (msg == NULL || buf == NULL || out_len == NULL) {
        return ANEM_DHCP_BADARG;
    }
    if (msg->header.hlen > ANEM_DHCP_CHADDR_SIZE) {
        return ANEM_DHCP_BADARG;
    }
    if (msg->option_count > ANEM_DHCP_MAX_OPTIONS) {
        return ANEM_DHCP_BADARG;
    }
    if (msg->option_count != 0u && msg->wire == NULL) {
        return ANEM_DHCP_BADARG;
    }

    w.buf = buf;
    w.cap = cap;
    w.pos = 0;
    w.overflow = false;
    put_header(&w, &msg->header);

    for (unsigned i = 0; i < (unsigned)msg->option_count; i++) {
        const AnemDhcpOption *opt = &msg->options[i];

        if (opt->code == ANEM_DHCP_OPT_PAD) {
            put_u8(&w, ANEM_DHCP_OPT_PAD);
            continue;
        }
        if ((size_t)opt->off >= msg->wire_len
            || msg->wire_len - (size_t)opt->off
                   < 2u + (size_t)opt->len) {
            return ANEM_DHCP_BADARG;
        }
        put_u8(&w, opt->code);
        put_u8(&w, opt->len);
        put_bytes(&w, msg->wire + opt->off + 2u, (size_t)opt->len);
    }
    put_u8(&w, ANEM_DHCP_OPT_END);
    for (uint16_t i = 0; i < msg->pad_len; i++) {
        put_u8(&w, 0);
    }
    if (w.overflow) {
        return ANEM_DHCP_NOSPACE;
    }
    *out_len = w.pos;
    return ANEM_DHCP_OK;
}

/* ---- builder ---------------------------------------------------------- */

AnemDhcpStatus anem_dhcp_header_init(AnemDhcpHeader *hdr)
{
    if (hdr == NULL) {
        return ANEM_DHCP_BADARG;
    }
    zero_bytes(hdr, sizeof *hdr);
    hdr->op = ANEM_DHCP_BOOTREQUEST;
    hdr->htype = ANEM_DHCP_HTYPE_ETHERNET;
    hdr->hlen = 6;
    return ANEM_DHCP_OK;
}

AnemDhcpStatus anem_dhcp_build_begin(AnemDhcpWriter *w, void *buf, size_t cap,
                                     const AnemDhcpHeader *hdr)
{
    DhcpWriterState s;

    if (w == NULL || buf == NULL || hdr == NULL) {
        return ANEM_DHCP_BADARG;
    }
    if (hdr->hlen > ANEM_DHCP_CHADDR_SIZE) {
        return ANEM_DHCP_BADARG;
    }
    zero_bytes(w, sizeof *w);
    s.buf = buf;
    s.cap = cap;
    s.pos = 0;
    s.overflow = false;
    put_header(&s, hdr);
    if (s.overflow) {
        return ANEM_DHCP_NOSPACE;
    }
    w->buf = buf;
    w->cap = cap;
    w->pos = s.pos;
    w->started = true;
    return ANEM_DHCP_OK;
}

AnemDhcpStatus anem_dhcp_build_option(AnemDhcpWriter *w, uint8_t code,
                                      const void *data, uint8_t len)
{
    DhcpWriterState s;

    if (w == NULL || !w->started) {
        return ANEM_DHCP_BADARG;
    }
    if (code == ANEM_DHCP_OPT_PAD || code == ANEM_DHCP_OPT_END) {
        return ANEM_DHCP_BADARG;
    }
    if (len != 0u && data == NULL) {
        return ANEM_DHCP_BADARG;
    }
    if (w->option_count >= ANEM_DHCP_MAX_OPTIONS) {
        return ANEM_DHCP_TOO_MANY;
    }
    s.buf = w->buf;
    s.cap = w->cap;
    s.pos = w->pos;
    s.overflow = false;
    put_u8(&s, code);
    put_u8(&s, len);
    put_bytes(&s, data, (size_t)len);
    if (s.overflow) {
        return ANEM_DHCP_NOSPACE;
    }
    w->pos = s.pos;
    w->option_count++;
    return ANEM_DHCP_OK;
}

AnemDhcpStatus anem_dhcp_build_option_u8(AnemDhcpWriter *w, uint8_t code,
                                         uint8_t value)
{
    return anem_dhcp_build_option(w, code, &value, 1);
}

AnemDhcpStatus anem_dhcp_build_option_u16(AnemDhcpWriter *w, uint8_t code,
                                          uint16_t value)
{
    uint8_t b[2];

    b[0] = (uint8_t)(value >> 8);
    b[1] = (uint8_t)(value & 0xffu);
    return anem_dhcp_build_option(w, code, b, (uint8_t)sizeof b);
}

AnemDhcpStatus anem_dhcp_build_option_u32(AnemDhcpWriter *w, uint8_t code,
                                          uint32_t value)
{
    uint8_t b[4];

    b[0] = (uint8_t)(value >> 24);
    b[1] = (uint8_t)((value >> 16) & 0xffu);
    b[2] = (uint8_t)((value >> 8) & 0xffu);
    b[3] = (uint8_t)(value & 0xffu);
    return anem_dhcp_build_option(w, code, b, (uint8_t)sizeof b);
}

AnemDhcpStatus anem_dhcp_build_finish(AnemDhcpWriter *w, size_t *out_len)
{
    DhcpWriterState s;

    if (w == NULL || out_len == NULL || !w->started) {
        return ANEM_DHCP_BADARG;
    }
    s.buf = w->buf;
    s.cap = w->cap;
    s.pos = w->pos;
    s.overflow = false;
    put_u8(&s, ANEM_DHCP_OPT_END);
    if (s.overflow) {
        return ANEM_DHCP_NOSPACE;
    }
    w->pos = s.pos;
    w->started = false;
    *out_len = w->pos;
    return ANEM_DHCP_OK;
}

/* ---- option access ---------------------------------------------------- */

const AnemDhcpOption *anem_dhcp_find_option(const AnemDhcpMessage *msg,
                                            uint8_t code, unsigned nth)
{
    unsigned seen = 0;

    if (msg == NULL) {
        return NULL;
    }
    for (unsigned i = 0; i < (unsigned)msg->option_count; i++) {
        if (msg->options[i].code == code) {
            if (seen == nth) {
                return &msg->options[i];
            }
            seen++;
        }
    }
    return NULL;
}

const uint8_t *anem_dhcp_option_data(const AnemDhcpMessage *msg,
                                     const AnemDhcpOption *opt)
{
    if (msg == NULL || msg->wire == NULL || opt == NULL
        || opt->code == ANEM_DHCP_OPT_PAD) {
        return NULL;
    }
    if ((size_t)opt->off >= msg->wire_len
        || msg->wire_len - (size_t)opt->off < 2u + (size_t)opt->len) {
        return NULL;
    }
    return msg->wire + opt->off + 2u;
}

AnemDhcpStatus anem_dhcp_option_u8(const AnemDhcpMessage *msg, uint8_t code,
                                   uint8_t *value)
{
    const AnemDhcpOption *opt;
    const uint8_t *data;

    if (msg == NULL || value == NULL) {
        return ANEM_DHCP_BADARG;
    }
    opt = anem_dhcp_find_option(msg, code, 0);
    if (opt == NULL) {
        return ANEM_DHCP_NOT_FOUND;
    }
    if (opt->len != 1u) {
        return ANEM_DHCP_FORMAT;
    }
    data = anem_dhcp_option_data(msg, opt);
    if (data == NULL) {
        return ANEM_DHCP_FORMAT;
    }
    *value = data[0];
    return ANEM_DHCP_OK;
}

AnemDhcpStatus anem_dhcp_option_u16(const AnemDhcpMessage *msg, uint8_t code,
                                    uint16_t *value)
{
    const AnemDhcpOption *opt;
    const uint8_t *data;

    if (msg == NULL || value == NULL) {
        return ANEM_DHCP_BADARG;
    }
    opt = anem_dhcp_find_option(msg, code, 0);
    if (opt == NULL) {
        return ANEM_DHCP_NOT_FOUND;
    }
    if (opt->len != 2u) {
        return ANEM_DHCP_FORMAT;
    }
    data = anem_dhcp_option_data(msg, opt);
    if (data == NULL) {
        return ANEM_DHCP_FORMAT;
    }
    *value = rd_u16(data);
    return ANEM_DHCP_OK;
}

AnemDhcpStatus anem_dhcp_option_u32(const AnemDhcpMessage *msg, uint8_t code,
                                    uint32_t *value)
{
    const AnemDhcpOption *opt;
    const uint8_t *data;

    if (msg == NULL || value == NULL) {
        return ANEM_DHCP_BADARG;
    }
    opt = anem_dhcp_find_option(msg, code, 0);
    if (opt == NULL) {
        return ANEM_DHCP_NOT_FOUND;
    }
    if (opt->len != 4u) {
        return ANEM_DHCP_FORMAT;
    }
    data = anem_dhcp_option_data(msg, opt);
    if (data == NULL) {
        return ANEM_DHCP_FORMAT;
    }
    *value = rd_u32(data);
    return ANEM_DHCP_OK;
}
