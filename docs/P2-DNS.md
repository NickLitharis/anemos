# P2 DNS: the wire codec, and an oracle that is not the implementation

`anemos/dns/` is Anemos's first P2 unit: a parser and serialiser for the DNS
message format of **RFC 1035** — header, question section, answer/authority/
additional sections, name compression in both directions, and the RDATA layouts
that are defined to carry domain names (NS, CNAME, SOA, PTR, MX, SRV).

It is a codec, not a resolver. It makes no network call and keeps no cache. That
is deliberate: the RFC fixes every byte, so this unit can be proved without the
VM. The resolver-shaped layer sits on top in
`anemos/dns/dns_client.{h,c}` (`docs/P2-DNS-CLIENT.md`): response correlation,
RCODE/TC handling and CNAME chains, still without a socket.

There is **no Microsoft source in this directory**. The wire format is the
specification; Microsoft's resolver was not read. XP's own observable resolver
behaviour becomes relevant when the client is wired to the stack (P4), not here.

## Shape

```c
AnemDnsStatus anem_dns_parse(const void *wire, size_t wire_len, AnemDnsMessage *out);
AnemDnsStatus anem_dns_write(const AnemDnsMessage *msg, void *buf, size_t cap, size_t *out_len);
AnemDnsStatus anem_dns_build_query(void *buf, size_t cap, uint16_t id, uint16_t flags,
                                   const AnemDnsName *qname, uint16_t qtype,
                                   uint16_t qclass, size_t *out_len);
AnemDnsStatus anem_dns_name_at(const AnemDnsMessage *msg, uint16_t off, AnemDnsName *out);
AnemDnsStatus anem_dns_rdata_name(const AnemDnsMessage *msg, const AnemDnsRecord *rr,
                                  AnemDnsName *out);
AnemDnsStatus anem_dns_name_to_text(const AnemDnsName *name, char *buf, size_t cap, size_t *out_len);
AnemDnsStatus anem_dns_name_from_text(const char *text, AnemDnsName *out);
```

A parsed message is a **zero-copy view**. `AnemDnsMessage` holds scalars and
offsets into the caller's wire buffer; names are decompressed by `name_at()` when
asked for, and RDATA is reached through offsets. Nothing is allocated, so §1 of
the coding rules has nothing to police. On i686 the view is 812 bytes
(`AnemDnsName` 258, `AnemDnsRecord` 16); it is caller storage, not a hidden heap.

The codec is freestanding: `dns.c` includes no libc header and uses three local
byte loops instead of `memcpy`/`memset`/`memcmp`. It compiles for
`i686-windows-msvc` with the pinned clang and links where a kernel replacement
would.

## Reading is strict on purpose

Every malformed-input class has its own status, because "rejected" is not enough
when a differential test is comparing two implementations:

| Status | Meaning |
|---|---|
| `ANEM_DNS_SHORT` | ends inside a fixed-size field (header, qtype/qclass, RR fixed part) |
| `ANEM_DNS_TRUNCATED` | a name or RDATA runs past the end of the message |
| `ANEM_DNS_BAD_LABEL` | label > 63, reserved label bits (`01`/`10`), or a name > 255 octets |
| `ANEM_DNS_BAD_POINTER` | compression pointer loops, points forward, or is out of range |
| `ANEM_DNS_BAD_RDLENGTH` | name-carrying RDATA does not fill `rdlength` exactly |
| `ANEM_DNS_TOO_MANY` | a section count exceeds the fixed view capacity |
| `ANEM_DNS_TRAILING` | bytes remain after the last section |
| `ANEM_DNS_TOO_LONG` | input exceeds 65,535 bytes |
| `ANEM_DNS_NOSPACE` | output buffer too small |
| `ANEM_DNS_FORMAT` | presentation-form name is malformed (or a hand-edited view is inconsistent) |

Two choices are strict where some implementations are lenient, and they are
deliberate:

- **Compression pointers must point strictly backwards.** RFC 1035 §4.1.4
  requires a pointer to a prior occurrence. This is also the proof that
  decompression terminates: every hop strictly decreases the offset. A hop
  counter is kept as a second bound (§13).
- **Trailing bytes are an error.** A DNS message over UDP has an exact length;
  accepting slack would let two implementations disagree about what was parsed.

RDATA is opaque except for the six types above. That is not a shortcut: only
those types are defined to contain a domain name, and only those can therefore
contain a compression pointer that the codec must rewrite. For every other type
the bytes are copied verbatim.

## Writing is canonical

The writer decompresses every name and re-compresses it with one deterministic
rule:

> For each name, find the **longest suffix** already emitted (among label
> boundaries, at offsets below 0x4000), and point at the **lowest** such offset;
> use it when the suffix is longer than the pointer (more than 2 bytes).

The label-offset table is bounded at 512 entries; filling it reduces compression
but never changes the decoded message. The 16 KB pointer limit is respected.

This is what makes "parse → write → identical bytes" a meaningful claim: the
golden vectors in `test_dns.c` were authored by hand from the RFC and are
reproduced byte for byte, including their `C00C`-style pointers. For inputs that
chose a different but valid compression, the invariant is weaker and still
tested: parse → write → parse yields the same message, and write → parse → write
is a fixed point.

## How it is verified

`anemos/dns/run.sh` — 24 checks for codec + client, all able to fail.
The codec-specific ones:

1. **Vector suite, i686 PE under Wine.** Seven hand-written golden messages
   (query, CNAME→A, MX, SOA, SRV, root, single label). Each must parse, write
   back to the **exact** input bytes, reparse to the same header, expose the
   expected names, survive presentation-form round-tripping, succeed at exact
   buffer size, and fail with `ANEM_DNS_NOSPACE` one byte short. Eleven
   malformed vectors pin one status per class, plus the >255-octet name and the
   >65,535-byte message.
2. **A weakened reader must fail the suite.** The negative build accepts
   forward/self pointers and reserved label bits; the malformed vectors then
   disagree. A check that cannot fail is not a check.
3. **Differential corpus against an independent implementation.** `dns_ref.py`
   implements the same format a second time in Python — parser, classifier,
   canonical writer — and is itself checked against the hand-written vectors.
   It generates 1,365 cases: the golden messages, the malformed list, every
   truncation prefix of every golden message, and deterministic single-byte
   mutations, insertions and deletions. Both implementations must agree on the
   **status code, the canonical bytes (FNV-1a) and the reparse status** for all
   1,365. The C side runs under ASan and UBSan. The weakened reader must
   disagree on the same corpus, proving the differential gate is sensitive.
4. **Freestanding object + size budget.** `dns.c` compiles for
   `i686-windows-msvc` and its `.text` is **4,776 bytes**, under the 8 KB
   per-translation-unit budget. The client is 1,117 bytes and is gated the same
   way.

Corpus coverage of the error classes (from `out/got.txt`): 428 accepted,
203 `SHORT`, 287 `TRUNCATED`, 154 `BAD_LABEL`, 36 `BAD_POINTER`, 2
`BAD_RDLENGTH`, 229 `TOO_MANY`, 26 `TRAILING`. Acceptance is exercised as hard
as rejection; a corpus that only tested failures would miss the whole happy
path.

Reproduce:

```sh
anemos/dns/run.sh
```

## Limits, stated rather than discovered later

- **Fixed section capacities**: 4 questions, 32 answers, 8 authority, 8
  additional. Larger messages are rejected with `ANEM_DNS_TOO_MANY`. This is a
  view-size decision, not an RFC limit; a streaming iterator is the way to lift
  it without growing the view, and is not built yet.
- **No EDNS(0) semantics.** OPT (type 41) parses as an opaque record; the
  extended RCODE, UDP size and options are not interpreted. EDNS belongs with
  the transport work, not the base codec.
- **No TCP length prefix.** The codec takes one message body. The two-byte TCP
  framing is a transport concern.
- **Unknown RDATA is copied verbatim.** If some future type compresses a name
  inside RDATA, this codec would not rewrite the pointer and the output could be
  invalid. RFC 1035 defines no such type; the six that do are handled.
- **The writer trusts a parsed view.** If a caller edits offsets or counts by
  hand, the writer validates bounds and returns `ANEM_DNS_FORMAT` instead of
  reading out of bounds, but it does not re-run the full parse. Views come from
  `anem_dns_parse()`.

## What this does and does not prove

It proves the codec matches RFC 1035 as read by two independent
implementations, including on 1,365 malformed and mutated inputs, and that it
behaves identically when built for the destination ABI. The client layer is
proved separately over 2,040 query/response cases (`docs/P2-DNS-CLIENT.md`).

It does **not** prove XP's resolver accepts the same messages. That is a
behavioural question about XP, and the oracle for it is the guest. It becomes
testable when the DNS client is wired to XP's socket path in P4; until then,
claiming XP compatibility for DNS would be exactly the kind of unverified
success this project forbids.
