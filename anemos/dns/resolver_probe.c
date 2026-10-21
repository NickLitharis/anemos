/* Trace harness for the DNS resolver: start/packet/timer/status/result, with
 * the socket core's request queue printed after every command so the glue is
 * diffable, not just the DNS status. */
#include "resolver.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static AnemResolver resolver;

static void print_requests(void)
{
    AnemSockRequest request;

    while (anem_socket_take_request(&request) != 0) {
        switch (request.kind) {
        case ANEM_REQUEST_OPEN:
            printf("req open %d %u\n", request.socket, request.value);
            break;
        case ANEM_REQUEST_BIND:
            printf("req bind %d %s %u\n", request.socket,
                   anem_inet_ntoa(request.addr), request.port);
            break;
        case ANEM_REQUEST_CONNECT:
            printf("req connect %d %s %u\n", request.socket,
                   anem_inet_ntoa(request.addr), request.port);
            break;
        case ANEM_REQUEST_SEND:
            printf("req send %d %u\n", request.socket, request.value);
            break;
        case ANEM_REQUEST_CLOSE:
            printf("req close %d\n", request.socket);
            break;
        default:
            printf("req other %d %u\n", request.socket, request.value);
            break;
        }
    }
}

static uint16_t parse_type(const char *text)
{
    if (strcmp(text, "A") == 0) {
        return ANEM_DNS_TYPE_A;
    }
    if (strcmp(text, "AAAA") == 0) {
        return ANEM_DNS_TYPE_AAAA;
    }
    return 0;
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

static size_t decode_hex(const char *text, uint8_t *out, size_t cap)
{
    size_t len = strlen(text);
    size_t bytes = 0;

    if (len % 2u != 0u || len / 2u > cap) {
        return (size_t)-1;
    }
    for (size_t i = 0; i < len; i += 2) {
        int hi = hex_nibble(text[i]);
        int lo = hex_nibble(text[i + 1]);

        if (hi < 0 || lo < 0) {
            return (size_t)-1;
        }
        out[bytes++] = (uint8_t)((hi << 4) | lo);
    }
    return bytes;
}

/* The negative build lives here, not in shipped resolver.c: it reports a
 * successful lookup as a DNS failure so the gate must reject it. */
static AnemResolverStatus report(AnemResolverStatus status)
{
#ifdef ANEM_RESOLVER_NEGATIVE
    return status == ANEM_RESOLVER_OK ? ANEM_RESOLVER_DNS : status;
#else
    return status;
#endif
}

static void print_result(void)
{
    const AnemDnsResult *result = anem_resolver_result(&resolver);
    AnemResolverStatus status = report(anem_resolver_state(&resolver));

    if (status != ANEM_RESOLVER_OK || result == NULL) {
        printf("result %s - - -\n", anem_resolver_status_name(status));
        return;
    }
    if (result->family == 4) {
        uint32_t addr = ((uint32_t)result->addr[0] << 24)
                      | ((uint32_t)result->addr[1] << 16)
                      | ((uint32_t)result->addr[2] << 8)
                      | (uint32_t)result->addr[3];

        printf("result ok 4 %s %lu\n", anem_inet_ntoa(anem_htonl(addr)),
               (unsigned long)result->ttl);
    } else {
        printf("result ok 6 ");
        for (unsigned i = 0; i < 16; i++) {
            printf("%02x", result->addr[i]);
        }
        printf(" %lu\n", (unsigned long)result->ttl);
    }
}

static uint64_t parse_u64(const char *text)
{
    char *end = NULL;
    unsigned long long value = strtoull(text, &end, 10);

    return (end == text || end == NULL || *end != '\0') ? 0u : (uint64_t)value;
}

int main(void)
{
    char line[1024];

    anem_socket_reset();
    anem_resolver_init(&resolver, 0, 1, 0);   /* idle, not pending */
    while (fgets(line, sizeof line, stdin) != NULL) {
        char *fields[10] = { 0 };
        unsigned count = 0;
        char *p = line;
        size_t len = strlen(line);

        while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r')) {
            line[--len] = '\0';
        }
        if (len == 0) {
            continue;
        }
        fields[count++] = p;
        for (; *p != '\0' && count < 10; p++) {
            if (*p == '\t') {
                *p = '\0';
                fields[count++] = p + 1;
            }
        }
        if (strcmp(fields[0], "init") == 0 && count >= 4) {
            anem_resolver_init(&resolver, (uint32_t)parse_u64(fields[1]),
                               (unsigned)parse_u64(fields[2]),
                               parse_u64(fields[3]));
            printf("init %s %s %s\n", fields[1], fields[2], fields[3]);
        } else if (strcmp(fields[0], "start") == 0 && count >= 5) {
            uint16_t qtype = parse_type(fields[2]);
            uint32_t server = 0;
            AnemResolverStatus status;

            if (!anem_inet_pton4(fields[3], &server)) {
                status = anem_resolver_cancel(&resolver);
            } else {
                status = anem_resolver_start(&resolver, fields[1], qtype,
                                             server, parse_u64(fields[4]));
            }
            printf("start %s %s %s\n", fields[1], fields[2],
                   anem_resolver_status_name(report(status)));
        } else if (strcmp(fields[0], "packet") == 0 && count >= 3) {
            uint8_t wire[2048];
            size_t wire_len = decode_hex(fields[1], wire, sizeof wire);
            uint64_t now = parse_u64(fields[2]);
            AnemResolverStatus status = (wire_len == (size_t)-1)
                ? ANEM_RESOLVER_BADARG
                : anem_resolver_on_packet(&resolver, wire, wire_len,
                                          (uint64_t)now);
            printf("packet %s\n", anem_resolver_status_name(report(status)));
        } else if (strcmp(fields[0], "timer") == 0 && count >= 2) {
            uint64_t now = parse_u64(fields[1]);
            printf("timer %s\n", anem_resolver_status_name(report(
                       anem_resolver_on_timer(&resolver, (uint64_t)now))));
        } else if (strcmp(fields[0], "status") == 0) {
            printf("status %s\n", anem_resolver_status_name(
                       report(anem_resolver_state(&resolver))));
        } else if (strcmp(fields[0], "result") == 0) {
            print_result();
        } else if (strcmp(fields[0], "client") == 0) {
            printf("client %d\n",
                   (int)anem_resolver_client_status(&resolver));
        } else {
            printf("? %s\n", fields[0]);
        }
        print_requests();
    }
    return 0;
}
