/* Differential driver: classify a corpus with the C codec and print one line
 * per case, in the format dns_ref.py emits:
 *
 *   index parse_status write_status canonical_fnv1a reparse_status
 *
 * -1 means "not attempted": the parse failed, or the write failed.
 *
 * The corpus file is a sequence of <u32 little-endian length><bytes>.
 */
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>

#include "dns.h"

static uint32_t fnv1a(const uint8_t *data, size_t len)
{
    uint32_t h = 2166136261u;

    for (size_t i = 0; i < len; i++) {
        h ^= (uint32_t)data[i];
        h *= 16777619u;
    }
    return h;
}

int main(int argc, char **argv)
{
    static uint8_t wire[65536];
    static uint8_t out[65536];
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
        uint8_t hdr[4];
        size_t got = fread(hdr, 1u, sizeof hdr, f);
        uint32_t n;
        AnemDnsMessage msg;
        AnemDnsStatus st;

        if (got == 0u) {
            break;
        }
        if (got != sizeof hdr) {
            fprintf(stderr, "truncated corpus header at case %u\n", index);
            (void)fclose(f);
            return 2;
        }
        n = (uint32_t)hdr[0] | ((uint32_t)hdr[1] << 8)
          | ((uint32_t)hdr[2] << 16) | ((uint32_t)hdr[3] << 24);
        if (n > sizeof wire) {
            fprintf(stderr, "case %u too long (%u)\n", index, n);
            (void)fclose(f);
            return 2;
        }
        if (fread(wire, 1u, n, f) != n) {
            fprintf(stderr, "truncated corpus case %u\n", index);
            (void)fclose(f);
            return 2;
        }
        st = anem_dns_parse(wire, n, &msg);
        if (st != ANEM_DNS_OK) {
            printf("%u %d -1 00000000 -1\n", index, (int)st);
        } else {
            size_t out_len = 0;
            AnemDnsStatus ws = anem_dns_write(&msg, out, sizeof out, &out_len);

            if (ws != ANEM_DNS_OK) {
                printf("%u 0 %d 00000000 -1\n", index, (int)ws);
            } else {
                AnemDnsMessage again;
                AnemDnsStatus st2 = anem_dns_parse(out, out_len, &again);

                printf("%u 0 0 %08x %d\n", index,
                       (unsigned)fnv1a(out, out_len), (int)st2);
            }
        }
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
