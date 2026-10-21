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
| P4.2 | Socket core: `socket`/`bind`/`connect`/`listen`/`accept`/`shutdown`/`close` state machine over an injectable transport — **done** | differential against an independent model, no sockets in the tests |
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

## P4.2 — the socket core (done)

`anemos/winsock/socket_core.*` is the connection lifecycle with no socket
under it. It keeps 64 slots, enforces state transitions, and emits requests
(`open`, `bind`, `listen`, `connect`, `shutdown`, `close`, `refuse`) that a
transport drains; completed-transport events (`connected`, `refused`,
`incoming`) are injected back in. Everything is a deterministic function of
the operation trace, so the gate compares full traces rather than sampling a
live machine.

Rules worth stating because they are choices, not accidents:

- `connect` on an unbound socket binds implicitly (emits `bind`); a stream
  socket becomes `connecting`, a datagram socket is `connected` at once
  because there is no handshake.
- A refused connect returns the socket to `bound`; it can be retried.
- `listen` accepts a bound stream socket and a repeated `listen` updates the
  backlog, as on Windows; a zero backlog becomes one.
- `accept` returns `WSAEWOULDBLOCK` on an empty queue, and a full backlog
  emits `refuse` to the transport instead of silently dropping.
- `bind` to the broadcast address fails with `WSAEADDRNOTAVAIL`. Port
  conflicts are checked by port (not address); an ephemeral port is taken
  from 49152 upward and skips ports already in use.
- `shutdown` uses Winsock's `SD_RECEIVE`/`SD_SEND`/`SD_BOTH` values and keeps
  the socket connected.
- A full request queue sets an overflow flag instead of silently losing an
  operation; the trace harness reports it.

The gate (`anemos/winsock/socket_run.sh`, 5 checks) runs 31 traces — eleven
hand-written scenarios and 20 seeded random walks — through the C core and an
independent Python model and requires byte-identical output, including the
drained request queue. A build with a broken accept queue is rejected by the
same corpus, and a 30,000-operation fuzz run stayed identical.
`socket_core.c` is 1410 bytes of `.text`.

**Limit:** this slice covers lifecycle only. No data path, no readiness, no
timing; those are P4.3. It also does not claim XP behaviour — Winsock's own
edge cases (for example `listen` on a datagram socket) still need the P4.6
probe.

## Reproducing

```sh
anemos/winsock/run.sh          # P4.1 address layer: 6/6
anemos/winsock/socket_run.sh   # P4.2 socket core: 5/5
```
