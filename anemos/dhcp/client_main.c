/* Trace driver for the DHCP client state machine.
 *
 * Reads a text trace (init/start/tick/recv) and prints the same snapshot
 * format as dhcp_client_ref.py after every operation, so the two
 * implementations can be diffed line by line.
 */
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "dhcp_client.h"

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

static void print_hex(const uint8_t *data, size_t len)
{
    for (size_t i = 0; i < len; i++) {
        printf("%02x", data[i]);
    }
}

static void snapshot(unsigned index, int status, const AnemDhcpClient *cl,
                     const uint8_t *packet, size_t packet_len)
{
    printf("%u %d %d %u %u %u %u %u %u %u %llu %llu %llu %llu %u ",
           index, status, (int)cl->state, cl->xid, cl->offered_ip, cl->ip,
           cl->server_id, cl->lease_secs, cl->t1_secs, cl->t2_secs,
           (unsigned long long)cl->next_ms,
           (unsigned long long)cl->t1_at_ms,
           (unsigned long long)cl->t2_at_ms,
           (unsigned long long)cl->lease_at_ms, cl->attempts);
    if (packet == NULL) {
        printf("-\n");
    } else {
        print_hex(packet, packet_len);
        printf("\n");
    }
}

int main(int argc, char **argv)
{
    static char line[4096];
    static uint8_t in[4096];
    static uint8_t out[1024];
    AnemDhcpClient cl;
    unsigned index = 0;
    FILE *f;

    if (argc != 2) {
        fprintf(stderr, "usage: %s trace.txt\n", argv[0]);
        return 2;
    }
    f = fopen(argv[1], "r");
    if (f == NULL) {
        fprintf(stderr, "cannot open %s\n", argv[1]);
        return 2;
    }
    while (fgets(line, sizeof line, f) != NULL) {
        char op[8];
        AnemDhcpStatus st = ANEM_DHCP_OK;
        size_t out_len = 0;
        const uint8_t *packet = NULL;

        if (sscanf(line, "%7s", op) != 1) {
            continue;
        }
        if (strcmp(op, "init") == 0) {
            unsigned seed = 0;
            char hex[64];
            size_t n = 0;

            if (sscanf(line, "init %u %63s", &seed, hex) != 2
                || !hex_decode(hex, in, sizeof in, &n)
                || n != ANEM_DHCP_CHADDR_SIZE) {
                fprintf(stderr, "bad init line at %u\n", index);
                (void)fclose(f);
                return 2;
            }
            st = anem_dhcp_client_init(&cl, seed, in, 6);
        } else if (strcmp(op, "start") == 0 || strcmp(op, "tick") == 0) {
            unsigned long long now = 0;

            if (sscanf(line, "%*s %llu", &now) != 1) {
                fprintf(stderr, "bad time at %u\n", index);
                (void)fclose(f);
                return 2;
            }
            if (strcmp(op, "start") == 0) {
                st = anem_dhcp_client_start(&cl, (uint64_t)now, out,
                                            sizeof out, &out_len);
            } else {
                st = anem_dhcp_client_tick(&cl, (uint64_t)now, out,
                                           sizeof out, &out_len);
            }
        } else if (strcmp(op, "recv") == 0) {
            unsigned long long now = 0;
            char hex[4096];
            size_t n = 0;

            if (sscanf(line, "recv %llu %4095s", &now, hex) != 2
                || !hex_decode(hex, in, sizeof in, &n)) {
                fprintf(stderr, "bad recv line at %u\n", index);
                (void)fclose(f);
                return 2;
            }
            st = anem_dhcp_client_receive(&cl, (uint64_t)now, in, n, out,
                                          sizeof out, &out_len);
        } else {
            fprintf(stderr, "bad operation at %u\n", index);
            (void)fclose(f);
            return 2;
        }
        if (out_len != 0u) {
            packet = out;
        }
        snapshot(index, (int)st, &cl, packet, out_len);
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
