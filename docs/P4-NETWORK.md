# P4 — the network stack

P4 is the last and largest replacement: XP's `net/tcpip` tree is ~288 C files,
and the gate is **Winsock conformance, with throughput not worse than XP's**.
This document is the slice plan; it will change as measurements arrive.

P4 started on 2026-10-21, before the P3 XP gates closed, by project direction.
That is a deliberate ordering exception: the address layer below is
host-testable and independent of the provider's guest behaviour. The P3
remainder (RAND hardening, the XP guest run, the signature decision) is still
open and is not claimed by anything here.

## The open architectural decision

`docs/OPEN-REPLACEMENT-PLAN.md` names the two honest options and neither is
chosen yet:

1. **An lwIP-style clean rewrite** behind the Winsock/TDI surface. Full
   control and licence clarity; the most work, and it must re-earn every
   behaviour XP already has.
2. **Adopt ReactOS's stack** behind the same surface. Far less code to write,
   but it imports a GPL/LGPL dependency and changes what this project's
   licence can be.

That decision belongs after the surface is measured, not before. Slices
P4.1-P4.4 are written against a transport seam so either option can supply
the bottom layer.

## Slices

| Slice | Work | Gate |
|---|---|---|
| P4.1 | Address layer: byte order, IPv4 text parse/format, `sockaddr_in`, `inet_*` names | differential corpus against an independent implementation; XP probe later |
| P4.2 | Socket core: `socket`/`bind`/`connect`/`listen`/`accept`/`shutdown`/`close` state machine over an injectable transport | differential against an independent model, no sockets in the tests |
| P4.3 | Readiness: `select`/`poll`, timeouts, non-blocking semantics, cancellation | deterministic scenario suite |
| P4.4 | The XP socket path for the DNS resolver, closing P2's deferred gap | resolver differential over the socket core |
| P4.5 | Transport provider: TDI/WSK or the adopted stack, decided by measurement | throughput not worse than XP |
| P4.6 | Winsock conformance probe in XP | observed behaviour identical to XP's own Winsock |

## P4.1 — the address layer (done)

`anemos/winsock/` implements `ANEM_AF_INET`, `sockaddr_in`, `htons`/`htonl`
and their inverses, `inet_pton`/`inet_ntop` (IPv4), and the legacy
`inet_addr`/`inet_ntoa` names. The parser is strict: four decimal octets, no
leading zeros, value ≤ 255 — the POSIX `inet_pton` form. `INADDR_NONE` remains
ambiguous with `255.255.255.255`, as it is on Windows.

The corpus gate (`anemos/winsock/run.sh`, 6 checks) compares 586 cases
against Python's `ipaddress`, including malformed, whitespace and leading-zero
forms, and refuses a deliberately wrong renderer. `addr.c` is 438 bytes of
`.text`.

**Limit, stated plainly:** this proves RFC/POSIX behaviour, not XP's. XP's
legacy `inet_addr` accepts forms the strict parser rejects, and that difference
is exactly what the P4.6 in-guest probe must record. Until then the module does
not claim XP compatibility for those inputs.

## Reproducing

```sh
anemos/winsock/run.sh    # 6/6, Wine, no VM
```
