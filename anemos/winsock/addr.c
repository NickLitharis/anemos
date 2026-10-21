#include "winsock.h"

#include <stdio.h>
#include <string.h>

_Static_assert(sizeof(AnemSockAddrIn) == 16,
               "sockaddr_in is 16 bytes on Winsock");
_Static_assert(offsetof(AnemSockAddrIn, family) == 0,
               "sin_family is first");
_Static_assert(offsetof(AnemSockAddrIn, port) == 2, "sin_port follows family");
_Static_assert(offsetof(AnemSockAddrIn, addr) == 4, "sin_addr follows port");
_Static_assert(offsetof(AnemSockAddrIn, zero) == 8, "sin_zero follows addr");

uint16_t anem_htons(uint16_t value)
{
    return (uint16_t)(((value & UINT16_C(0x00ff)) << 8)
                      | ((value & UINT16_C(0xff00)) >> 8));
}

uint16_t anem_ntohs(uint16_t value)
{
    return anem_htons(value);
}

uint32_t anem_htonl(uint32_t value)
{
    return ((value & UINT32_C(0x000000ff)) << 24)
         | ((value & UINT32_C(0x0000ff00)) << 8)
         | ((value & UINT32_C(0x00ff0000)) >> 8)
         | ((value & UINT32_C(0xff000000)) >> 24);
}

uint32_t anem_ntohl(uint32_t value)
{
    return anem_htonl(value);
}

/* One decimal field: 1-3 digits, no leading zero unless the field is "0",
 * value <= 255. Advances *pp only on success. */
static int parse_octet(const char **pp, uint32_t *out)
{
    const char *p = *pp;
    uint32_t value = 0;
    unsigned digits = 0;

    if (*p < '0' || *p > '9') {
        return 0;
    }
    if (p[0] == '0' && p[1] >= '0' && p[1] <= '9') {
        return 0;                       /* leading zero: ambiguous octal */
    }
    while (digits < 3 && *p >= '0' && *p <= '9') {
        value = value * 10u + (uint32_t)(*p - '0');
        p++;
        digits++;
    }
    if (*p >= '0' && *p <= '9') {
        return 0;                       /* a fourth digit */
    }
    if (value > 255u) {
        return 0;
    }
    *pp = p;
    *out = value;
    return 1;
}

int anem_inet_pton4(const char *src, uint32_t *dst_net)
{
    const char *p = src;
    uint32_t octet[4];
    uint32_t address;

    if (src == NULL || dst_net == NULL) {
        return 0;
    }
    for (unsigned i = 0; i < 4u; i++) {
        if (!parse_octet(&p, &octet[i])) {
            return 0;
        }
        if (i < 3u) {
            if (*p != '.') {
                return 0;
            }
            p++;
        }
    }
    if (*p != '\0') {
        return 0;
    }
    /* Store Winsock's representation: the octets in memory order. */
    address = (octet[0] << 24) | (octet[1] << 16) | (octet[2] << 8) | octet[3];
    *dst_net = anem_htonl(address);
    return 1;
}

const char *anem_inet_ntop4(uint32_t src_net, char *dst, size_t size)
{
    unsigned char bytes[4];
    int written;

    if (dst == NULL || size < ANEM_INET_ADDRSTRLEN) {
        return NULL;
    }
    src_net = anem_ntohl(src_net);       /* memory order -> dotted-quad value */
    bytes[0] = (unsigned char)(src_net >> 24);
    bytes[1] = (unsigned char)(src_net >> 16);
    bytes[2] = (unsigned char)(src_net >> 8);
    bytes[3] = (unsigned char)src_net;
#ifdef ANEM_WS_NEGATIVE
    /* Negative control: the gate must reject a build whose rendering is
     * wrong by one. Only the test build defines this. */
    bytes[0] = (unsigned char)(bytes[0] ^ 0x01u);
#endif
    written = snprintf(dst, size, "%u.%u.%u.%u",
                       (unsigned)bytes[0], (unsigned)bytes[1],
                       (unsigned)bytes[2], (unsigned)bytes[3]);
    if (written < 0 || (size_t)written >= size) {
        return NULL;
    }
    return dst;
}

/* BSD/XP legacy form: 1-4 parts, each decimal, octal (leading 0) or hex
 * (0x). A leading space is XP's quirk: inet_addr(" 1.2.3.4") returns 0, not
 * INADDR_NONE. The strict inet_pton path above deliberately stays strict. */
static int parse_legacy_part(const char **pp, uint32_t *out)
{
    const char *p = *pp;
    unsigned base = 10;
    uint32_t value = 0;
    unsigned digits = 0;

    if (p[0] == '0' && (p[1] == 'x' || p[1] == 'X')) {
        base = 16;
        p += 2;
    } else if (p[0] == '0' && p[1] != '\0' && p[1] != '.') {
        base = 8;
        p += 1;
    }
    for (;;) {
        unsigned digit;

        if (*p >= '0' && *p <= '9') {
            digit = (unsigned)(*p - '0');
        } else if (base == 16 && *p >= 'a' && *p <= 'f') {
            digit = (unsigned)(*p - 'a') + 10u;
        } else if (base == 16 && *p >= 'A' && *p <= 'F') {
            digit = (unsigned)(*p - 'A') + 10u;
        } else {
            break;
        }
        if (digit >= base || value > (UINT32_MAX - digit) / base) {
            return 0;
        }
        value = value * base + digit;
        digits++;
        p++;
    }
    if (digits == 0u) {
        return 0;
    }
    *pp = p;
    *out = value;
    return 1;
}

uint32_t anem_inet_addr(const char *src)
{
    uint32_t parts[4];
    unsigned count = 0;
    const char *p = src;
    uint32_t value;

    if (src == NULL) {
        return ANEM_INADDR_NONE;
    }
    if (*p == ' ') {
        return 0;                        /* the XP quirk, recorded by P4.6 */
    }
    while (*p != '\0' && count < 4u) {
        if (!parse_legacy_part(&p, &parts[count])) {
            return ANEM_INADDR_NONE;
        }
        count++;
        if (*p == '.') {
            p++;
            if (*p == '\0') {
                return ANEM_INADDR_NONE;
            }
        } else if (*p != '\0') {
            return ANEM_INADDR_NONE;
        }
    }
    if (*p != '\0' || count == 0u) {
        return ANEM_INADDR_NONE;
    }
    for (unsigned i = 0; i + 1u < count; i++) {
        if (parts[i] > 255u) {
            return ANEM_INADDR_NONE;
        }
    }
    switch (count) {
    case 1:
        return anem_htonl(parts[0]);
    case 2:
        if (parts[1] > UINT32_C(0x00ffffff)) return ANEM_INADDR_NONE;
        value = (parts[0] << 24) | parts[1];
        break;
    case 3:
        if (parts[2] > UINT32_C(0x0000ffff)) return ANEM_INADDR_NONE;
        value = (parts[0] << 24) | (parts[1] << 16) | parts[2];
        break;
    default:
        if (parts[3] > 255u) return ANEM_INADDR_NONE;
        value = (parts[0] << 24) | (parts[1] << 16) | (parts[2] << 8) | parts[3];
        break;
    }
    return anem_htonl(value);
}

const char *anem_inet_ntoa(uint32_t src_net)
{
    static char buffer[ANEM_INET_ADDRSTRLEN];

    return anem_inet_ntop4(src_net, buffer, sizeof buffer);
}

void anem_sockaddr_in_init(AnemSockAddrIn *sa, uint32_t addr_net,
                           uint16_t port_host)
{
    if (sa == NULL) {
        return;
    }
    memset(sa, 0, sizeof *sa);
    sa->family = (uint16_t)ANEM_AF_INET;
    sa->port = anem_htons(port_host);
    sa->addr = addr_net;
}
