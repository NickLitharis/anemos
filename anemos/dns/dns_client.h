/* DNS client — request/response correlation and address extraction.
 *
 * This sits on anemos/dns/: the caller sends the query bytes and hands back
 * received frames. It owns no socket, no timers and no server list; transport
 * and retry policy belong above it. One query may be pending at a time.
 *
 * Scope: A and AAAA queries, including CNAME chains inside one response. A
 * chain that ends without an address is reported as CNAME with the target, so
 * the caller can issue the next query. PTR/MX/SRV extraction is left to callers
 * using the codec directly.
 *
 * xids come from a caller-seeded LCG (same rule as the DHCP client): seed from
 * entropy in production, seed fixed for reproducible traces.
 *
 * Written from RFC 1035. No Microsoft source.
 */
#ifndef ANEMOS_DNS_CLIENT_H
#define ANEMOS_DNS_CLIENT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "dns.h"

typedef enum AnemDnsClientStatus {
    ANEM_DNS_CLIENT_OK = 0,
    ANEM_DNS_CLIENT_BADARG,      /* NULL pointer or unsupported qtype */
    ANEM_DNS_CLIENT_BADSTATE,    /* no pending query, or one already pending */
    ANEM_DNS_CLIENT_IGNORED,     /* not a matching response */
    ANEM_DNS_CLIENT_MALFORMED,   /* the codec rejected the frame */
    ANEM_DNS_CLIENT_TRUNCATED,   /* TC bit set; retry over TCP */
    ANEM_DNS_CLIENT_RCODE,       /* server returned a non-zero RCODE */
    ANEM_DNS_CLIENT_NO_ANSWER,   /* matching response, no A/AAAA for the name */
    ANEM_DNS_CLIENT_CNAME,       /* chain ended at a different name */
    ANEM_DNS_CLIENT_LOOP,        /* CNAME cycle or chain longer than the message */
    ANEM_DNS_CLIENT_NOSPACE      /* output buffer too small */
} AnemDnsClientStatus;

typedef struct AnemDnsResult {
    int codec_status;      /* AnemDnsStatus when status is MALFORMED, else 0 */
    uint16_t rcode;        /* DNS RCODE when status is RCODE */
    uint8_t family;        /* 4 or 6 when status is OK */
    uint8_t addr[16];      /* network order; first 4 used for A */
    uint32_t ttl;          /* TTL of the address record */
    AnemDnsName name;      /* final name (OK) or CNAME target (CNAME) */
} AnemDnsResult;

typedef struct AnemDnsClient {
    uint32_t seed;
    uint16_t id;
    uint16_t qtype;
    uint16_t qclass;
    bool pending;
    AnemDnsName qname;
} AnemDnsClient;

AnemDnsClientStatus anem_dns_client_init(AnemDnsClient *cl, uint32_t seed);

/* Drop a pending transaction without touching the LCG state, so the next
 * query gets a fresh id. Used when a resolver start replaces an in-flight
 * lookup. */
void anem_dns_client_abort(AnemDnsClient *cl);

/* Build a recursive query into `out`. qtype must be A or AAAA. The details
 * needed to match the response are stored in `cl`. BADSTATE if a query is
 * already pending. */
AnemDnsClientStatus anem_dns_client_query(AnemDnsClient *cl,
                                          const AnemDnsName *name,
                                          uint16_t qtype, uint16_t qclass,
                                          void *out, size_t cap,
                                          size_t *out_len);

/* Match one received frame against the pending query. On OK/RCODE/CNAME/
 * NO_ANSWER/LOOP/TRUNCATED the transaction is consumed (pending becomes
 * false). IGNORED and MALFORMED leave it pending, so a stray frame does not
 * destroy the transaction. */
AnemDnsClientStatus anem_dns_client_receive(AnemDnsClient *cl,
                                            const void *wire, size_t wire_len,
                                            AnemDnsResult *result);

#endif /* ANEMOS_DNS_CLIENT_H */
