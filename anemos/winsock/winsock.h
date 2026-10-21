/* Winsock address layer: byte order, IPv4 text conversion, sockaddr_in.
 *
 * P4 starts here because every socket call and both candidate stack
 * approaches (lwIP-style, or ReactOS behind TDI/WSK) need exactly this
 * surface. The parser is deliberately strict — four decimal octets, no
 * leading zeros — which is POSIX inet_pton's documented form and what
 * modern independent implementations (Python's ipaddress) accept. XP's
 * legacy quirks are an oracle question for the P4 conformance probe; this
 * unit claims RFC/POSIX behaviour, not XP behaviour, yet.
 *
 * Network byte order is big-endian and every integer here is stored in that
 * order, exactly like Winsock's in_addr.S_un.S_addr.
 */
#ifndef ANEM_WINSOCK_H
#define ANEM_WINSOCK_H

#include <stddef.h>
#include <stdint.h>

#define ANEM_AF_INET 2
#define ANEM_INADDR_NONE UINT32_C(0xffffffff)
#define ANEM_INET_ADDRSTRLEN 16

/* Winsock sockaddr_in: family in host order, port and address in network
 * order, eight zero bytes of padding. `addr` is Winsock's u_long: its MEMORY
 * bytes are the address octets in order, so on a little-endian host its
 * numeric value is byte-swapped relative to the dotted-quad integer. That is
 * the representation XP observes (validated by the P4.6 probe). Layout is
 * guarded by _Static_assert in addr.c so an ABI drift cannot pass unnoticed. */
typedef struct AnemSockAddrIn {
    uint16_t family;
    uint16_t port;
    uint32_t addr;
    uint8_t zero[8];
} AnemSockAddrIn;

uint16_t anem_htons(uint16_t value);
uint16_t anem_ntohs(uint16_t value);
uint32_t anem_htonl(uint32_t value);
uint32_t anem_ntohl(uint32_t value);

/* Strict dotted-decimal parse. `dst_net` receives the address in network byte
 * order. Returns 1 on success, 0 on any malformed input. */
int anem_inet_pton4(const char *src, uint32_t *dst_net);

/* Format a network-order address. Returns `dst`, or NULL when `dst` is NULL
 * or `size` cannot hold "255.255.255.255" plus the terminator. */
const char *anem_inet_ntop4(uint32_t src_net, char *dst, size_t size);

/* Legacy names from Winsock. Both use the same strict parser; INADDR_NONE is
 * therefore ambiguous with 255.255.255.255, as it is on Windows. `ntoa`
 * returns a static buffer and is not thread-safe, matching Winsock. */
uint32_t anem_inet_addr(const char *src);
const char *anem_inet_ntoa(uint32_t src_net);

/* Fill a Winsock-shaped sockaddr_in; `port_host` is in host order. */
void anem_sockaddr_in_init(AnemSockAddrIn *sa, uint32_t addr_net,
                           uint16_t port_host);

#endif
