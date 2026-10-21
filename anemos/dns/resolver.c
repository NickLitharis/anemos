#include "resolver.h"

#include <string.h>

void anem_resolver_init(AnemResolver *resolver, uint32_t seed,
                        unsigned max_attempts, uint64_t timeout_ms)
{
    if (resolver == NULL) {
        return;
    }
    memset(resolver, 0, sizeof *resolver);
    resolver->client_status = ANEM_DNS_CLIENT_OK;
    resolver->status = ANEM_RESOLVER_BADARG;
    resolver->max_attempts = (max_attempts == 0u) ? 1u : max_attempts;
    resolver->timeout_ms = timeout_ms;
    resolver->seed = seed;
    (void)anem_dns_client_init(&resolver->client, seed);
}

const char *anem_resolver_status_name(AnemResolverStatus status)
{
    switch (status) {
    case ANEM_RESOLVER_PENDING: return "pending";
    case ANEM_RESOLVER_OK: return "ok";
    case ANEM_RESOLVER_BADARG: return "badarg";
    case ANEM_RESOLVER_DNS: return "dns";
    case ANEM_RESOLVER_TIMEOUT: return "timeout";
    case ANEM_RESOLVER_TRANSPORT: return "transport";
    }
    return "unknown";
}

static uint64_t deadline_after(uint64_t now, uint64_t timeout)
{
    return (timeout > UINT64_MAX - now) ? UINT64_MAX : now + timeout;
}

/* The socket core numbers sockets from 1, so 0 is a safe "none" sentinel. */
static void close_socket(AnemResolver *resolver)
{
    if (resolver->sock != 0) {
        (void)anem_socket_close(resolver->sock);
        resolver->sock = 0;
    }
}

/* Fresh datagram socket, connected to the server, with the query queued.
 * Every retry goes through here: a retransmission on the same socket would
 * depend on the transport restoring writability, which is exactly the kind of
 * hidden coupling this seam avoids. */
static AnemResolverStatus open_and_send(AnemResolver *resolver)
{
    int id = anem_socket_socket(ANEM_SOCK_DGRAM);

    if (id < 0) {
        return ANEM_RESOLVER_TRANSPORT;
    }
    resolver->sock = id;
    if (anem_socket_connect(id, resolver->server, ANEM_RESOLVER_PORT) < 0) {
        close_socket(resolver);
        return ANEM_RESOLVER_TRANSPORT;
    }
    if (anem_socket_send(id, resolver->query,
                         (unsigned)resolver->query_len) < 0) {
        close_socket(resolver);
        return ANEM_RESOLVER_TRANSPORT;
    }
    return ANEM_RESOLVER_PENDING;
}

AnemResolverStatus anem_resolver_cancel(AnemResolver *resolver)
{
    if (resolver == NULL) {
        return ANEM_RESOLVER_BADARG;
    }
    close_socket(resolver);
    resolver->status = ANEM_RESOLVER_BADARG;
    return resolver->status;
}

AnemResolverStatus anem_resolver_start(AnemResolver *resolver, const char *name,
                                       uint16_t qtype, uint32_t server,
                                       uint64_t now_ms)
{
    AnemDnsName qname;
    AnemDnsStatus name_status;

    if (resolver == NULL || name == NULL) {
        return ANEM_RESOLVER_BADARG;
    }
    if (qtype != ANEM_DNS_TYPE_A && qtype != ANEM_DNS_TYPE_AAAA) {
        return anem_resolver_cancel(resolver);
    }
    name_status = anem_dns_name_from_text(name, &qname);
    if (name_status != ANEM_DNS_OK) {
        return anem_resolver_cancel(resolver);
    }
    close_socket(resolver);
    anem_dns_client_abort(&resolver->client);
    resolver->server = server;
    resolver->qtype = qtype;
    resolver->status = ANEM_RESOLVER_PENDING;
    if (anem_dns_client_query(&resolver->client, &qname, qtype,
                              ANEM_DNS_CLASS_IN, resolver->query,
                              sizeof resolver->query,
                              &resolver->query_len) != ANEM_DNS_CLIENT_OK) {
        resolver->status = ANEM_RESOLVER_BADARG;
        return resolver->status;
    }
    resolver->attempts = 1;
    resolver->deadline_ms = deadline_after(now_ms, resolver->timeout_ms);
    resolver->status = open_and_send(resolver);
    return resolver->status;
}

AnemResolverStatus anem_resolver_on_packet(AnemResolver *resolver,
                                           const void *wire, size_t wire_len,
                                           uint64_t now_ms)
{
    AnemDnsClientStatus received;

    (void)now_ms;
    if (resolver == NULL) {
        return ANEM_RESOLVER_BADARG;
    }
    if (resolver->status != ANEM_RESOLVER_PENDING) {
        return resolver->status;
    }
    received = anem_dns_client_receive(&resolver->client, wire, wire_len,
                                       &resolver->result);
    resolver->client_status = received;
    switch (received) {
    case ANEM_DNS_CLIENT_OK:
        resolver->status = ANEM_RESOLVER_OK;
        close_socket(resolver);
        break;
    case ANEM_DNS_CLIENT_IGNORED:
    case ANEM_DNS_CLIENT_MALFORMED:
        /* Not ours or not parsable: keep waiting for the real answer. */
        resolver->status = ANEM_RESOLVER_PENDING;
        break;
    case ANEM_DNS_CLIENT_BADARG:
    case ANEM_DNS_CLIENT_BADSTATE:
        resolver->status = ANEM_RESOLVER_BADARG;
        close_socket(resolver);
        break;
    default:
        resolver->status = ANEM_RESOLVER_DNS;
        close_socket(resolver);
        break;
    }
    return resolver->status;
}

AnemResolverStatus anem_resolver_on_timer(AnemResolver *resolver,
                                          uint64_t now_ms)
{
    if (resolver == NULL) {
        return ANEM_RESOLVER_BADARG;
    }
    if (resolver->status != ANEM_RESOLVER_PENDING) {
        return resolver->status;
    }
    if (now_ms < resolver->deadline_ms) {
        return ANEM_RESOLVER_PENDING;
    }
    if (resolver->attempts >= resolver->max_attempts) {
        resolver->status = ANEM_RESOLVER_TIMEOUT;
        close_socket(resolver);
        return resolver->status;
    }
    resolver->attempts++;
    close_socket(resolver);
    resolver->status = open_and_send(resolver);
    if (resolver->status == ANEM_RESOLVER_PENDING) {
        resolver->deadline_ms = deadline_after(now_ms, resolver->timeout_ms);
    }
    return resolver->status;
}

AnemResolverStatus anem_resolver_state(const AnemResolver *resolver)
{
    return resolver == NULL ? ANEM_RESOLVER_BADARG : resolver->status;
}

const uint8_t *anem_resolver_query(const AnemResolver *resolver, size_t *len)
{
    if (resolver == NULL || len == NULL) {
        return NULL;
    }
    *len = resolver->query_len;
    return resolver->query;
}

const AnemDnsResult *anem_resolver_result(const AnemResolver *resolver)
{
    return resolver == NULL ? NULL : &resolver->result;
}

AnemDnsClientStatus anem_resolver_client_status(const AnemResolver *resolver)
{
    return resolver == NULL ? ANEM_DNS_CLIENT_BADARG : resolver->client_status;
}
