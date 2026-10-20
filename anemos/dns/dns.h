/* DNS wire codec — RFC 1035 message format.
 *
 * This is P2 of the Anemos plan: a host-side parse/serialise unit for the DNS
 * message format, written from RFC 1035 (and RFC 3596 for the AAAA type
 * number only). No Microsoft source was consulted; the wire format is the
 * specification.
 *
 * Scope:
 *   - header, question section, and the answer/authority/additional sections;
 *   - name decompression on read, pointer compression on write;
 *   - canonical RDATA handling for the types that carry a domain name
 *     (NS, CNAME, SOA, PTR, MX, SRV). Every other type is opaque bytes,
 *     which is correct: only those types are defined to contain names that
 *     may be compressed (RFC 1035 §4.1.4, §3.3.13-14, RFC 2782 for SRV).
 *
 * Model: a parsed message is a zero-copy view. AnemDnsMessage holds scalars
 * and offsets into the caller's wire buffer; it borrows that buffer and must
 * not outlive it. Nothing is allocated. anem_dns_name_at() decompresses a
 * name when the caller wants one, so the view stays small enough to embed.
 *
 * Limits are explicit and checked: a message with more questions or records
 * than the fixed capacities is rejected with ANEM_DNS_TOO_MANY rather than
 * silently truncated.
 *
 * No allocation, no global state, every function that can fail returns an
 * AnemDnsStatus (OS-CODING-RULES §1, §4, §11). Every loop is bounded (§13).
 */
#ifndef ANEMOS_DNS_H
#define ANEMOS_DNS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define ANEM_DNS_HEADER_SIZE 12u
#define ANEM_DNS_MAX_WIRE 65535u
#define ANEM_DNS_MAX_NAME 255u
#define ANEM_DNS_MAX_LABEL 63u
#define ANEM_DNS_MAX_POINTER_HOPS 256u

/* Presentation form: worst case is 255 wire bytes each escaped as \DDD,
 * plus a dot between at most 127 labels, plus the terminator. */
#define ANEM_DNS_MAX_TEXT 1152u

/* Capacities of the zero-copy view. Counts above these are TOO_MANY. */
#define ANEM_DNS_MAX_QUESTIONS 4u
#define ANEM_DNS_MAX_ANSWERS 32u
#define ANEM_DNS_MAX_AUTHORITY 8u
#define ANEM_DNS_MAX_ADDITIONAL 8u

/* Compression table size for the writer. Filling it only degrades
 * compression; it never changes the decoded message. */
#define ANEM_DNS_MAX_COMPRESS 512u

/* RR types the codec understands structurally. The rest are opaque. */
enum AnemDnsType {
    ANEM_DNS_TYPE_A = 1,
    ANEM_DNS_TYPE_NS = 2,
    ANEM_DNS_TYPE_CNAME = 5,
    ANEM_DNS_TYPE_SOA = 6,
    ANEM_DNS_TYPE_PTR = 12,
    ANEM_DNS_TYPE_MX = 15,
    ANEM_DNS_TYPE_TXT = 16,
    ANEM_DNS_TYPE_AAAA = 28,
    ANEM_DNS_TYPE_SRV = 33,
    ANEM_DNS_TYPE_OPT = 41
};

enum AnemDnsClass {
    ANEM_DNS_CLASS_IN = 1,
    ANEM_DNS_CLASS_ANY = 255
};

/* Header flag bits (RFC 1035 §4.1.1). */
#define ANEM_DNS_FLAG_QR 0x8000u
#define ANEM_DNS_FLAG_AA 0x0400u
#define ANEM_DNS_FLAG_TC 0x0200u
#define ANEM_DNS_FLAG_RD 0x0100u
#define ANEM_DNS_FLAG_RA 0x0080u

typedef enum AnemDnsStatus {
    ANEM_DNS_OK = 0,
    ANEM_DNS_BADARG,      /* NULL pointer or an argument that breaks a contract */
    ANEM_DNS_TOO_LONG,    /* wire message longer than 65535 bytes */
    ANEM_DNS_SHORT,       /* ends inside a fixed-size field */
    ANEM_DNS_TRUNCATED,   /* a name or RDATA runs past the end of the message */
    ANEM_DNS_BAD_LABEL,   /* label > 63, reserved label bits, or name > 255 */
    ANEM_DNS_BAD_POINTER, /* pointer loop, forward reference, or out of range */
    ANEM_DNS_BAD_RDLENGTH,/* name-carrying RDATA does not fill rdlength exactly */
    ANEM_DNS_TOO_MANY,    /* section count exceeds the fixed capacity */
    ANEM_DNS_TRAILING,    /* bytes remain after the last section */
    ANEM_DNS_NOSPACE,     /* output buffer too small */
    ANEM_DNS_FORMAT,      /* presentation-form name is malformed */
    ANEM_DNS_NO_NAME      /* record type carries no domain name in RDATA */
} AnemDnsStatus;

typedef struct AnemDnsHeader {
    uint16_t id;
    uint16_t flags;
    uint16_t qdcount;
    uint16_t ancount;
    uint16_t nscount;
    uint16_t arcount;
} AnemDnsHeader;

/* A decompressed name in wire form: length-prefixed labels, final zero.
 * len counts every octet including the root label. */
typedef struct AnemDnsName {
    uint8_t wire[ANEM_DNS_MAX_NAME];
    uint16_t len;
} AnemDnsName;

typedef struct AnemDnsQuestion {
    uint16_t name_off; /* offset of the (possibly compressed) name in wire */
    uint16_t type;
    uint16_t class_;
} AnemDnsQuestion;

typedef struct AnemDnsRecord {
    uint16_t name_off;  /* offset of the (possibly compressed) owner name */
    uint16_t type;
    uint16_t class_;
    uint32_t ttl;
    uint16_t rdata_off; /* offset of RDATA in wire */
    uint16_t rdlength;
} AnemDnsRecord;

/* A parsed message. `wire` is borrowed, not owned. */
typedef struct AnemDnsMessage {
    const uint8_t *wire;
    size_t wire_len;
    AnemDnsHeader header;
    AnemDnsQuestion questions[ANEM_DNS_MAX_QUESTIONS];
    AnemDnsRecord answers[ANEM_DNS_MAX_ANSWERS];
    AnemDnsRecord authority[ANEM_DNS_MAX_AUTHORITY];
    AnemDnsRecord additional[ANEM_DNS_MAX_ADDITIONAL];
} AnemDnsMessage;

/* Parse and fully validate a message, including every name it contains and
 * the RDATA of the name-carrying types. On success `out` borrows `wire`.
 *
 * Strict by design: trailing bytes are ANEM_DNS_TRAILING, not ignored. Every
 * malformed-input class has its own status so a differential test can compare
 * classifications, not just pass/fail. */
AnemDnsStatus anem_dns_parse(const void *wire, size_t wire_len,
                             AnemDnsMessage *out);

/* Serialise a parsed message. Names are decompressed and re-compressed with
 * the deterministic longest-suffix rule; RDATA of name-carrying types is
 * re-encoded from the source wire. Opaque RDATA is copied verbatim.
 *
 * A message produced by anem_dns_parse() and written back is byte-identical
 * to its input when the input used the same canonical compression rule; in
 * every case the result parses to the same message (see run.sh). */
AnemDnsStatus anem_dns_write(const AnemDnsMessage *msg, void *buf, size_t cap,
                             size_t *out_len);

/* Build a one-question query. flags is written verbatim; use
 * ANEM_DNS_FLAG_RD for a recursive query. */
AnemDnsStatus anem_dns_build_query(void *buf, size_t cap, uint16_t id,
                                   uint16_t flags, const AnemDnsName *qname,
                                   uint16_t qtype, uint16_t qclass,
                                   size_t *out_len);

/* Decompress the name at `off`. */
AnemDnsStatus anem_dns_name_at(const AnemDnsMessage *msg, uint16_t off,
                               AnemDnsName *out);

/* First domain name in a record's RDATA: the name for NS/CNAME/PTR, the
 * MNAME for SOA, the exchange for MX, the target for SRV. */
AnemDnsStatus anem_dns_rdata_name(const AnemDnsMessage *msg,
                                  const AnemDnsRecord *rr, AnemDnsName *out);

/* Presentation form, RFC 1035 §5.1: labels joined by '.', no trailing dot,
 * root is ".". Bytes that are not printable ASCII are escaped as \DDD; '.'
 * and '\' are escaped with a backslash. `out_len` excludes the terminator. */
AnemDnsStatus anem_dns_name_to_text(const AnemDnsName *name, char *buf,
                                    size_t cap, size_t *out_len);

/* Parse a presentation-form name. A trailing dot is accepted; "." is the
 * root. Rejects empty labels, labels over 63 bytes, names over 255 wire
 * bytes, and malformed escapes. */
AnemDnsStatus anem_dns_name_from_text(const char *text, AnemDnsName *out);

#endif /* ANEMOS_DNS_H */
