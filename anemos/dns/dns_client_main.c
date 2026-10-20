/* Differential driver for the DNS client.
 *
 * Corpus records: <u16 LE qname_len><qname wire><u16 LE qtype>
 *                 <u32 LE response_len><response>
 *
 * For each case: fresh client (seed 1), build the query, deliver the response,
 * print: index status codec_status rcode family addr_hex ttl name_hex
 */
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "dns_client.h"

static bool read_exact(FILE *f, void *dst, size_t n)
{
    return fread(dst, 1u, n, f) == n;
}

static uint16_t le16(const uint8_t *p)
{
    return (uint16_t)((unsigned)p[0] | ((unsigned)p[1] << 8));
}

static uint32_t le32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8)
         | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static void print_hex(const uint8_t *data, size_t len)
{
    for (size_t i = 0; i < len; i++) {
        printf("%02x", data[i]);
    }
}

int main(int argc, char **argv)
{
    static uint8_t qname[256];
    static uint8_t resp[65536];
    static uint8_t query[65536];
    FILE *f;
    unsigned index = 0;

    if (argc != 2) {
        fprintf(stderr, "usage: %s corpus.bin\n", argv[0]);
        return 2;
    }
    f = fopen(argv[1], "rb");
    if (f == NULL) {
        fprintf(stderr, "cannot open %s\n", argv[1]);
        return 2;
    }
    for (;;) {
        uint8_t head[4];
        size_t got = fread(head, 1u, 2u, f);
        uint16_t qname_len;
        uint8_t typebuf[2];
        uint8_t lenbuf[4];
        uint32_t resp_len;
        AnemDnsName name;
        AnemDnsClient cl;
        AnemDnsResult result;
        AnemDnsClientStatus st;
        size_t query_len = 0;

        if (got == 0u) {
            break;
        }
        if (got != 2u) {
            fprintf(stderr, "truncated case header at %u\n", index);
            (void)fclose(f);
            return 2;
        }
        qname_len = le16(head);
        if (qname_len > sizeof qname || !read_exact(f, qname, qname_len)
            || !read_exact(f, typebuf, 2u)) {
            fprintf(stderr, "truncated case body at %u\n", index);
            (void)fclose(f);
            return 2;
        }
        if (!read_exact(f, lenbuf, 4u)) {
            fprintf(stderr, "truncated response length at %u\n", index);
            (void)fclose(f);
            return 2;
        }
        resp_len = le32(lenbuf);
        if (resp_len > sizeof resp || !read_exact(f, resp, resp_len)) {
            fprintf(stderr, "truncated response at %u\n", index);
            (void)fclose(f);
            return 2;
        }

        memset(&name, 0, sizeof name);
        if (qname_len == 0u || qname_len > ANEM_DNS_MAX_NAME) {
            printf("%u %d 0 0 0 - 0 -\n", index, (int)ANEM_DNS_CLIENT_BADARG);
            index++;
            continue;
        }
        memcpy(name.wire, qname, qname_len);
        name.len = qname_len;
        (void)anem_dns_client_init(&cl, 1u);
        st = anem_dns_client_query(&cl, &name, le16(typebuf), ANEM_DNS_CLASS_IN,
                                   query, sizeof query, &query_len);
        if (st != ANEM_DNS_CLIENT_OK) {
            printf("%u %d 0 0 0 - 0 -\n", index, (int)st);
            index++;
            continue;
        }
        st = anem_dns_client_receive(&cl, resp, resp_len, &result);
        printf("%u %d %d %u %u ", index, (int)st, result.codec_status,
               result.rcode, result.family);
        if (st == ANEM_DNS_CLIENT_OK) {
            print_hex(result.addr, (result.family == 4u) ? 4u : 16u);
        } else {
            printf("-");
        }
        printf(" %u ", result.ttl);
        if (result.name.len != 0u) {
            print_hex(result.name.wire, result.name.len);
        } else {
            printf("-");
        }
        printf("\n");
        index++;
    }
    if (ferror(f)) {
        fprintf(stderr, "read error\n");
        (void)fclose(f);
        return 2;
    }
    (void)fclose(f);
    return 0;
}
