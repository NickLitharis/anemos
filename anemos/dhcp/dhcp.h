/* DHCP wire codec — RFC 2131 messages with RFC 2132 options.
 *
 * This is the second P2 unit. It parses and serialises BOOTP/DHCP message
 * bodies: the fixed 236-byte header, the magic cookie, and the option stream.
 * It is a codec, not a client state machine; no socket, no timer, no lease.
 *
 * Written from RFC 2131 and RFC 2132. No Microsoft source was consulted.
 *
 * Model: a parsed message is a zero-copy view. The 236-byte header is copied
 * into AnemDhcpHeader (it is fixed and small); options are kept as offsets
 * into the caller's wire buffer. Nothing is allocated.
 *
 * Round-trip strength: every accepted message is reproduced byte for byte by
 * anem_dhcp_write(). Interior PAD options and zero padding after END are
 * preserved, so this is not a "canonical form" claim — it is exact.
 *
 * Strict by design:
 *   - the magic cookie is required (BOOTP messages without it are rejected);
 *   - the option stream must contain END;
 *   - bytes after END must be zero (padding), otherwise TRAILING;
 *   - hlen > 16 is rejected.
 *
 * Option overload (RFC 2132 §9.3) is not interpreted: an overload option is
 * returned like any other, and options are read only from the option field.
 * This is recorded in docs/P2-DHCP.md as a limit.
 *
 * Every function that can fail returns an AnemDhcpStatus (OS-CODING-RULES §4,
 * §11). Every loop is bounded (§13); dns.c's freestanding discipline applies
 * here too: no libc headers.
 */
#ifndef ANEMOS_DHCP_H
#define ANEMOS_DHCP_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define ANEM_DHCP_FIXED_SIZE 236u   /* through `file`, before the cookie */
#define ANEM_DHCP_MIN_SIZE 240u     /* fixed header + magic cookie */
#define ANEM_DHCP_MAX_WIRE 65535u
#define ANEM_DHCP_MAX_OPTIONS 64u
#define ANEM_DHCP_CHADDR_SIZE 16u
#define ANEM_DHCP_SNAME_SIZE 64u
#define ANEM_DHCP_FILE_SIZE 128u
#define ANEM_DHCP_MAGIC_COOKIE 0x63825363u

/* op */
enum AnemDhcpOp {
    ANEM_DHCP_BOOTREQUEST = 1,
    ANEM_DHCP_BOOTREPLY = 2
};

/* htype */
enum AnemDhcpHType {
    ANEM_DHCP_HTYPE_ETHERNET = 1
};

#define ANEM_DHCP_FLAG_BROADCAST 0x8000u

/* DHCP message type (option 53), RFC 2132 §9.6. */
enum AnemDhcpMessageType {
    ANEM_DHCP_DISCOVER = 1,
    ANEM_DHCP_OFFER = 2,
    ANEM_DHCP_REQUEST = 3,
    ANEM_DHCP_DECLINE = 4,
    ANEM_DHCP_ACK = 5,
    ANEM_DHCP_NAK = 6,
    ANEM_DHCP_RELEASE = 7,
    ANEM_DHCP_INFORM = 8
};

/* Option codes used by the codec's tests and clients (RFC 2132). */
enum AnemDhcpOptionCode {
    ANEM_DHCP_OPT_PAD = 0,
    ANEM_DHCP_OPT_SUBNET_MASK = 1,
    ANEM_DHCP_OPT_ROUTER = 3,
    ANEM_DHCP_OPT_DNS = 6,
    ANEM_DHCP_OPT_HOST_NAME = 12,
    ANEM_DHCP_OPT_DOMAIN_NAME = 15,
    ANEM_DHCP_OPT_MTU = 26,
    ANEM_DHCP_OPT_BROADCAST = 28,
    ANEM_DHCP_OPT_REQUESTED_IP = 50,
    ANEM_DHCP_OPT_LEASE_TIME = 51,
    ANEM_DHCP_OPT_OVERLOAD = 52,
    ANEM_DHCP_OPT_MESSAGE_TYPE = 53,
    ANEM_DHCP_OPT_SERVER_ID = 54,
    ANEM_DHCP_OPT_PARAM_LIST = 55,
    ANEM_DHCP_OPT_MESSAGE = 56,
    ANEM_DHCP_OPT_MAX_SIZE = 57,
    ANEM_DHCP_OPT_T1 = 58,
    ANEM_DHCP_OPT_T2 = 59,
    ANEM_DHCP_OPT_CLIENT_ID = 61,
    ANEM_DHCP_OPT_END = 255
};

typedef enum AnemDhcpStatus {
    ANEM_DHCP_OK = 0,
    ANEM_DHCP_BADARG,       /* NULL pointer or an argument that breaks a contract */
    ANEM_DHCP_TOO_LONG,     /* wire message longer than 65535 bytes */
    ANEM_DHCP_SHORT,        /* shorter than the 240-byte DHCP minimum */
    ANEM_DHCP_BAD_COOKIE,   /* magic cookie is not 99.130.83.99 */
    ANEM_DHCP_BAD_HLEN,     /* hlen > 16 */
    ANEM_DHCP_BAD_OPTION,   /* option length runs past the end, or no length byte */
    ANEM_DHCP_NO_END,       /* option stream ends without END (255) */
    ANEM_DHCP_TRAILING,     /* non-zero bytes after END */
    ANEM_DHCP_TOO_MANY,     /* more than ANEM_DHCP_MAX_OPTIONS entries */
    ANEM_DHCP_NOSPACE,      /* output buffer too small */
    ANEM_DHCP_FORMAT,       /* typed reader: option has the wrong length */
    ANEM_DHCP_NOT_FOUND,    /* typed reader: option absent */
    ANEM_DHCP_BADSTATE      /* client: operation not valid in this state */
} AnemDhcpStatus;

typedef struct AnemDhcpHeader {
    uint8_t op;
    uint8_t htype;
    uint8_t hlen;
    uint8_t hops;
    uint32_t xid;
    uint16_t secs;
    uint16_t flags;
    uint32_t ciaddr;
    uint32_t yiaddr;
    uint32_t siaddr;
    uint32_t giaddr;
    uint8_t chaddr[ANEM_DHCP_CHADDR_SIZE];
    uint8_t sname[ANEM_DHCP_SNAME_SIZE];
    uint8_t file[ANEM_DHCP_FILE_SIZE];
} AnemDhcpHeader;

/* One option entry. PAD (0) has len 0 and occupies one byte at off; every
 * other code occupies two bytes plus len data bytes at off. */
typedef struct AnemDhcpOption {
    uint16_t off;
    uint8_t code;
    uint8_t len;
} AnemDhcpOption;

typedef struct AnemDhcpMessage {
    const uint8_t *wire;
    size_t wire_len;
    AnemDhcpHeader header;
    uint16_t option_count;
    uint16_t pad_len; /* zero bytes after END, preserved by the writer */
    AnemDhcpOption options[ANEM_DHCP_MAX_OPTIONS];
} AnemDhcpMessage;

/* Parse and validate. `out` borrows `wire`. */
AnemDhcpStatus anem_dhcp_parse(const void *wire, size_t wire_len,
                               AnemDhcpMessage *out);

/* Serialise a parsed message, byte for byte. */
AnemDhcpStatus anem_dhcp_write(const AnemDhcpMessage *msg, void *buf,
                               size_t cap, size_t *out_len);

/* Header initialiser: BOOTREQUEST, Ethernet, hlen 6, everything else zero. */
AnemDhcpStatus anem_dhcp_header_init(AnemDhcpHeader *hdr);

/* Builder. begin() writes the fixed header and cookie; option() appends one
 * option (PAD and END are not accepted here — finish writes END); finish()
 * appends END and reports the length. The writer is caller storage. */
typedef struct AnemDhcpWriter {
    uint8_t *buf;
    size_t cap;
    size_t pos;
    unsigned option_count;
    bool overflow;
    bool started;
} AnemDhcpWriter;

AnemDhcpStatus anem_dhcp_build_begin(AnemDhcpWriter *w, void *buf, size_t cap,
                                     const AnemDhcpHeader *hdr);
AnemDhcpStatus anem_dhcp_build_option(AnemDhcpWriter *w, uint8_t code,
                                      const void *data, uint8_t len);
/* Network-order integer options. Use these rather than passing a host
 * integer to build_option(), which copies bytes verbatim. */
AnemDhcpStatus anem_dhcp_build_option_u8(AnemDhcpWriter *w, uint8_t code,
                                         uint8_t value);
AnemDhcpStatus anem_dhcp_build_option_u16(AnemDhcpWriter *w, uint8_t code,
                                          uint16_t value);
AnemDhcpStatus anem_dhcp_build_option_u32(AnemDhcpWriter *w, uint8_t code,
                                          uint32_t value);
AnemDhcpStatus anem_dhcp_build_finish(AnemDhcpWriter *w, size_t *out_len);

/* First option with `code` after the `nth` earlier ones (nth 0 = first).
 * Returns NULL when absent. PAD (0) can be found; END is not stored. */
const AnemDhcpOption *anem_dhcp_find_option(const AnemDhcpMessage *msg,
                                            uint8_t code, unsigned nth);

/* Typed readers. Absent -> NOT_FOUND; wrong length -> FORMAT. */
AnemDhcpStatus anem_dhcp_option_u8(const AnemDhcpMessage *msg, uint8_t code,
                                   uint8_t *value);
AnemDhcpStatus anem_dhcp_option_u16(const AnemDhcpMessage *msg, uint8_t code,
                                    uint16_t *value);
AnemDhcpStatus anem_dhcp_option_u32(const AnemDhcpMessage *msg, uint8_t code,
                                    uint32_t *value);

/* Pointer to an option's data bytes (NULL for PAD). */
const uint8_t *anem_dhcp_option_data(const AnemDhcpMessage *msg,
                                     const AnemDhcpOption *opt);

#endif /* ANEMOS_DHCP_H */
