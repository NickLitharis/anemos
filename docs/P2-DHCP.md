# P2 DHCP: the codec, with an exact round-trip

`anemos/dhcp/` is the second P2 unit: a parser and serialiser for DHCP
messages (RFC 2131) and their options (RFC 2132). It covers the fixed 236-byte
BOOTP header, the magic cookie, the option stream, the option vocabulary a
client needs, and a builder for DISCOVER/REQUEST-style messages.

It is a codec, not a client state machine. The state machine is a separate
unit on top (`anemos/dhcp/dhcp_client.{h,c}`, `docs/P2-DHCP-CLIENT.md`): it owns
the socket-free DISCOVER/OFFER/REQUEST/ACK flow and the timers. That separation
is deliberate — the bytes are fixed by the RFC and can be proved without a VM,
and the machine can then be diffed independently.

There is **no Microsoft source in this directory**. RFC 2131/2132 are the
specification. XP's DHCP client behaviour is not claimed here.

## Shape

```c
AnemDhcpStatus anem_dhcp_parse(const void *wire, size_t wire_len, AnemDhcpMessage *out);
AnemDhcpStatus anem_dhcp_write(const AnemDhcpMessage *msg, void *buf, size_t cap, size_t *out_len);

AnemDhcpStatus anem_dhcp_header_init(AnemDhcpHeader *hdr);
AnemDhcpStatus anem_dhcp_build_begin(AnemDhcpWriter *w, void *buf, size_t cap,
                                     const AnemDhcpHeader *hdr);
AnemDhcpStatus anem_dhcp_build_option(AnemDhcpWriter *w, uint8_t code,
                                      const void *data, uint8_t len);
AnemDhcpStatus anem_dhcp_build_finish(AnemDhcpWriter *w, size_t *out_len);

const AnemDhcpOption *anem_dhcp_find_option(const AnemDhcpMessage *msg, uint8_t code, unsigned nth);
AnemDhcpStatus anem_dhcp_option_u8/u16/u32(const AnemDhcpMessage *msg, uint8_t code, ...);
const uint8_t *anem_dhcp_option_data(const AnemDhcpMessage *msg, const AnemDhcpOption *opt);
```

A parsed message is a **zero-copy view**. The 236-byte header is copied into
`AnemDhcpHeader` (fixed, small, and useful on its own); options are kept as
`(offset, code, length)` entries into the caller's buffer. Nothing is
allocated. On i686 the view is 504 bytes (`AnemDhcpHeader` 236, `AnemDhcpOption` 4,
64 entries, two counts, wire pointer/length).

The codec is freestanding: no libc header, three local byte loops instead of
`memcpy`/`memset`, and it compiles for `i686-windows-msvc` with the pinned
clang.

## The round-trip is exact, not canonical

DHCP has no compression, so there is no canonicalisation step to argue about.
The writer emits the parsed header fields, then every option in order, then END,
then exactly the number of zero padding bytes that followed END in the input.
Interior PAD options are stored as entries and preserved. Therefore:

> For **every** message `anem_dhcp_parse()` accepts,
> `anem_dhcp_write()` reproduces the input byte for byte.

This is checked for every accepted case in the differential corpus, not just
for the golden vectors. It is a stronger property than the DNS unit can claim
(where compression permits several valid encodings of the same message).

## Reading is strict

| Status | Meaning |
|---|---|
| `ANEM_DHCP_SHORT` | shorter than the 240-byte DHCP minimum (header + cookie) |
| `ANEM_DHCP_BAD_COOKIE` | the four bytes after the fixed header are not 99.130.83.99 |
| `ANEM_DHCP_BAD_HLEN` | `hlen > 16`, which the fixed `chaddr` field cannot describe |
| `ANEM_DHCP_BAD_OPTION` | an option length runs past the end, or the length byte is missing |
| `ANEM_DHCP_NO_END` | the option stream ends without END (255) |
| `ANEM_DHCP_TRAILING` | non-zero bytes after END |
| `ANEM_DHCP_TOO_MANY` | more than 64 option entries |
| `ANEM_DHCP_TOO_LONG` | input exceeds 65,535 bytes |
| `ANEM_DHCP_NOSPACE` | output buffer too small |
| `ANEM_DHCP_FORMAT` | a typed reader found an option of the wrong width |
| `ANEM_DHCP_NOT_FOUND` | a typed reader found no such option |

Consequences worth stating:

- **A DHCP message without the magic cookie is rejected**, even though BOOTP
  allowed one. This codec implements DHCP; BOOTP compatibility is a separate
  decision.
- **Zero bytes after END are padding and are preserved; non-zero bytes are an
  error.** A hand-built message that happens to leave slack bytes is not
  silently accepted.
- **PAD options count toward the 64-entry view.** A sender that pads the option
  area with thousands of zeros before END is rejected with `TOO_MANY`; padding
  after END has no such cost.

## Integer options are network order

`anem_dhcp_build_option()` copies bytes verbatim, which is right for opaque
options and wrong for integers: passing a host `uint32_t` produces a
little-endian payload that looks fine in a loopback test and is wrong on the
wire. The typed builders `anem_dhcp_build_option_u8/u16/u32()` encode network
order and are what the client and the tests use. This was a real bug in the
client's first REQUEST, caught by the scenario suite — recorded here because the
same trap will be there for every future builder caller.

## Verification

`anemos/dhcp/run.sh` — 24 checks for the codec and client together, all able
to fail. The codec-specific ones:

1. **Vector suite, i686 PE under Wine.** Nine golden messages authored from the
   RFC: DISCOVER, OFFER, REQUEST, ACK, NAK, RELEASE, INFORM, a message with
   interior PADs and five zero bytes after END, and the 241-byte minimum. Each
   must parse, write back to the identical bytes, reparse with the same fields,
   succeed at exact buffer size and fail one byte short. Typed readers are
   checked for value, wrong width (`FORMAT`) and absence (`NOT_FOUND`); the
   builder must reproduce the DISCOVER and minimal vectors exactly.
2. **A weakened option-length check must fail the suite.** The negative build
   accepts an option that runs past the end; the malformed vectors then
   disagree.
3. **Differential corpus against an independent implementation.** `dhcp_ref.py`
   implements the same format a second time in Python and is itself checked
   against the golden vectors. It generates **4,095 cases**: goldens, malformed
   classes, every truncation prefix of every golden message, and deterministic
   single-byte mutations, insertions and deletions. The C side runs under ASan
   and UBSan. Both must agree on status, exact bytes (FNV-1a) and reparse
   status; the weakened build must disagree on the same corpus.
4. **Freestanding object + size budget.** `dhcp.c` compiles for
   `i686-windows-msvc` and its `.text` is **2,003 bytes**, under the 8 KB
   per-translation-unit budget.

Corpus coverage (from `out/got.txt`): 1,146 accepted, 2,162 `SHORT`, 527
`BAD_COOKIE`, 5 `BAD_HLEN`, 195 `BAD_OPTION`, 52 `NO_END`, 7 `TRAILING`, 1
`TOO_MANY`. `TOO_LONG` and `NOSPACE` are exercised in the vector suite, where
the inputs can be sized deliberately.

The high `SHORT` count is expected: truncating a 250-byte message at every
prefix produces many prefixes below 240 bytes, and every one must be rejected
without reading past the buffer. ASan would catch a mistake.

Reproduce:

```sh
anemos/dhcp/run.sh
```

## Limits, stated rather than discovered later

- **Option overload (RFC 2132 §9.3) is not interpreted.** The overload option
  (52) is returned like any other; options are read only from the option field,
  never from `sname`/`file`. A server that uses overload will have options the
  codec does not expose. This is the largest known functional gap.
- **Option concatenation (RFC 3396) is not implemented.** Repeated option codes
  are exposed as separate entries; longer values split across occurrences are
  not joined. A client that needs this can join them itself or wait for a later
  revision.
- **No BOOTP-without-cookie mode.** See above.
- **64 options, 255 bytes each.** The view is fixed-size by design; larger
  option sets are `TOO_MANY`. The 255-byte cap is the wire format's own limit
  for a single option.
- **No minimum-size padding policy.** RFC 2131 recommends clients pad to at
  least 300 bytes. The builder emits END and no padding; padding is a sender
  policy layered on top, not part of the encoding.
- **DECLINE and RELEASE are not triggered.** The builder can make either
  packet; the state machine (peer document) does not send them yet. Decline
  needs ARP conflict detection and RELEASE needs a shutdown hook.

## What this does and does not prove

It proves the codec matches RFC 2131/2132 as read by two independent
implementations across 4,095 accepted and rejected inputs, and that every
accepted message is reproduced exactly. It proves the destination-ABI build is
clean and small. The client state machine is verified separately by a scenario
suite and a six-trace differential against a second implementation
(`docs/P2-DHCP-CLIENT.md`).

It does **not** prove XP's DHCP client behaves identically. That is a
behavioural question, and the oracle is the guest. It becomes testable once the
client state machine and the socket path exist (P4); claiming XP compatibility
before then would be unverified success.
