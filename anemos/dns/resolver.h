/* DNS resolver over the socket core: the glue P2 deliberately left out.
 *
 * Builds the query with anemos/dns's client, owns a datagram socket through
 * the P4.2/4.3 socket core, retries on explicit time, and maps the client's
 * result. No allocation, no real socket, no ambient clock: the caller injects
 * packets and ticks the timer, which is what makes the differential traces
 * deterministic.
 *
 * Scope: A and AAAA, one server, in-message CNAME chains (the client walks
 * them). A chain that ends without an address is reported as DNS_CNAME for
 * the caller to restart; TCP fallback on the TC bit is not implemented and is
 * reported as DNS_TRUNCATED.
 */
#ifndef ANEMOS_RESOLVER_H
#define ANEMOS_RESOLVER_H

#include <stddef.h>
#include <stdint.h>

#include "../winsock/socket_core.h"
#include "dns_client.h"

#define ANEM_RESOLVER_MAX_QUERY 512u
#define ANEM_RESOLVER_PORT 53u

typedef enum AnemResolverStatus {
    ANEM_RESOLVER_PENDING = 0,
    ANEM_RESOLVER_OK,
    ANEM_RESOLVER_BADARG,
    ANEM_RESOLVER_DNS,        /* dns_client_status says why */
    ANEM_RESOLVER_TIMEOUT,
    ANEM_RESOLVER_TRANSPORT   /* the socket core refused an operation */
} AnemResolverStatus;

typedef struct AnemResolver {
    AnemDnsClient client;
    AnemDnsResult result;
    AnemDnsClientStatus client_status;
    AnemResolverStatus status;
    int sock;
    uint32_t seed;
    uint32_t server;
    unsigned attempts;
    unsigned max_attempts;
    uint64_t timeout_ms;
    uint64_t deadline_ms;
    uint16_t qtype;
    uint8_t query[ANEM_RESOLVER_MAX_QUERY];
    size_t query_len;
} AnemResolver;

/* Configure once. `max_attempts` counts the first send, so 1 means no retry. */
void anem_resolver_init(AnemResolver *resolver, uint32_t seed,
                        unsigned max_attempts, uint64_t timeout_ms);

/* Begin a transaction. Returns PENDING on success, BADARG for an invalid name
 * or unsupported type. */
AnemResolverStatus anem_resolver_start(AnemResolver *resolver, const char *name,
                                       uint16_t qtype, uint32_t server,
                                       uint64_t now_ms);

/* Inject one received frame. Stray frames leave the transaction pending. */
AnemResolverStatus anem_resolver_on_packet(AnemResolver *resolver,
                                           const void *wire, size_t wire_len,
                                           uint64_t now_ms);

/* Advance the clock. Only fires on or after the deadline. */
AnemResolverStatus anem_resolver_on_timer(AnemResolver *resolver,
                                          uint64_t now_ms);

/* Abandon any transaction: close its socket (emitting the close request) and
 * mark the resolver BADARG. Safe when idle. */
AnemResolverStatus anem_resolver_cancel(AnemResolver *resolver);

AnemResolverStatus anem_resolver_state(const AnemResolver *resolver);
const uint8_t *anem_resolver_query(const AnemResolver *resolver, size_t *len);
const AnemDnsResult *anem_resolver_result(const AnemResolver *resolver);
AnemDnsClientStatus anem_resolver_client_status(const AnemResolver *resolver);

const char *anem_resolver_status_name(AnemResolverStatus status);

#endif /* ANEMOS_RESOLVER_H */
