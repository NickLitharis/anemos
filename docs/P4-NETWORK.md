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
| P4.3 | Readiness/data path: `select` over read/write sets, non-blocking send/recv, EOF/reset | differential against the same model — **done (no blocking wait; the caller polls)** |
| P4.4 | The socket path for the DNS resolver, closing P2's deferred gap — **done** | resolver differential over the socket core |
| P4.5 | Transport seam + loopback reference transport (**done**); the real provider (TDI/WSK or the adopted stack) is still the open decision | loopback scenarios; the real gate is throughput not worse than XP |
| P4.6 | Winsock conformance probe in XP — **probe and capture path done, execution blocked** | observed behaviour identical to XP's own Winsock |

## P4.1 — the address layer (done)

`anemos/winsock/` implements `ANEM_AF_INET`, `sockaddr_in`, `htons`/`htonl`
and their inverses, `inet_pton`/`inet_ntop` (IPv4), and the legacy
`inet_addr`/`inet_ntoa` names. The parser is strict: four decimal octets, no
leading zeros, value ≤ 255 — the POSIX `inet_pton` form. `INADDR_NONE` remains
ambiguous with `255.255.255.255`, as it is on Windows.

The corpus gate (`anemos/winsock/run.sh`, 6 checks) compares 586 cases
against Python's `ipaddress`, including malformed, whitespace and leading-zero
forms, and refuses a deliberately wrong renderer. `addr.c` is 442 bytes of
`.text`.

`in_addr.S_addr` is Winsock's `u_long`: its **memory bytes** are the address
octets in order, so on a little-endian host the numeric value is byte-swapped
relative to the dotted-quad integer. The P4.6 twin caught that the first
implementation stored the canonical integer, which would have made every
`S_addr` comparison and struct field wrong despite a correct layout. The
parser now stores `htonl(value)` and the formatters apply `ntohl` first.

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

## P4.3 — readiness and the data path (done)

The core now tracks what a transport would announce: queued receive bytes,
writability, graceful EOF and reset. `send` accepts bytes only while the
transport says the socket is writable and clears the flag until the transport
announces writability again; `recv` consumes from the announced byte count and
returns zero bytes on EOF; both return `WSAECONNRESET`/`WSAESHUTDOWN` where
Winsock does. `select` validates every id first, then reports read readiness
(data, EOF or reset) and write readiness (writable or reset); it **does not
wait** — the transport owns blocking, which is P4.5's job.

Review-driven semantics now in the core:

- a listening socket is read-ready while a connection is queued;
- a refused or reset connect leaves the socket `bound` with a pending error
  (`WSAECONNREFUSED` / `WSAECONNRESET`) that `select` surfaces as write-ready
  and `anem_socket_error` reports, like `SO_ERROR`;
- readiness means "will not block", so shutdown directions and pending errors
  make the corresponding set ready; a repeated shutdown is `WSAENOTCONN`;
- a reset clears buffered bytes, and `readable` is ignored after EOF;
- TCP and UDP port namespaces are separate;
- both parsers are strict and bounded, overlong trace lines are rejected
  instead of split, and CRLF traces behave like LF ones.

A datagram `connect` is immediately writable; the fuzz pass caught the model
disagreeing with that, and `dgram-connect` now pins it. The corpus is 40
traces and the C/Python diff includes the request queue and select bitmaps;
`anemos/winsock/socket_run.sh` is 7 checks. `socket_core.c` is 2269 bytes of
`.text`.

## P4.4 — the resolver over the socket path (done)

`anemos/dns/resolver.*` is the glue P2 left out. It builds the query with the
P2 client, opens a datagram socket through the P4 socket core, sends, takes
injected frames, and retries on explicit time. Nothing blocks and nothing
allocates: the caller delivers packets and ticks the timer.

Scope and deliberate limits:

- A and AAAA, one server, in-message CNAME chains (the P2 client walks them).
- A chain that ends without an address is `DNS` with client status `CNAME` for
  the caller to restart; the TC bit is `DNS` with `TRUNCATED` (no TCP fallback).
- A retry closes the old socket and opens a new one, so retransmission never
  depends on the transport restoring writability.
- A stray or malformed frame leaves the transaction pending; a frame after a
  terminal outcome is ignored.
- Starting again reuses the configured seed, so a transaction is reproducible.

The gate (`anemos/dns/resolver_run.sh`, 8 checks) runs 18 traces: basic A and
AAAA, wrong id then the real answer, NXDOMAIN, TC, malformed then retry, a
timer sequence through retry to timeout, CNAME chain, CNAME-only, empty answer,
invalid name, invalid server, an abandoned transaction, a restart, a packet
before `init`, an unsupported type, a trailing-dot name, and values that
exercise 32/64-bit masking. The Python model orchestrates the independent
Python socket and DNS-client models; the diff covers the DNS status, the
client status, and every socket request.

The review pass tightened it further: `start` aborts an in-flight client
without resetting its LCG, so a restart gets a fresh xid (retries keep it);
an invalid start cancels the old socket instead of leaking it; deadlines
saturate instead of wrapping; client `BADARG`/`BADSTATE` map to resolver
`BADARG`; the negative control now lives in the probe and must flip every
trace that expects `result ok`; the corpus is regenerated from scratch,
checked for its exact size, and asserted to reach ok, dns and timeout. Both
processes run under a timeout and their exit status is checked. `resolver.c`
is 914 bytes of `.text`; P2's own gate still reports 24/24.

## P4.5 — the transport seam and the loopback (done)

`anemos/winsock/loopback.*` is the reference implementation of the seam below
the socket core. It consumes the core's request queue — `open`, `bind`,
`listen`, `connect`, `accept`, `send` (now carrying the caller's bytes),
`shutdown`, `close`, `refuse` — and injects the events a real transport would:
`connected`, `refused`, `incoming`, `readable(bytes)`, `writable`, `eof`,
`reset`. Sockets are wired to each other in one process with bounded receive
queues, so the full user-space path runs without a kernel, a NIC or a stack.

`transport_probe.c` then exercises it end to end: TCP connect/accept and data
in both directions, EOF after `shutdown(SD_SEND)` and after close, refusal of
an unbound port (asserting `WSAECONNREFUSED` and the return to `bound`), a
one-deep backlog whose second connection is reset, and UDP in both directions.
The gate (`anemos/winsock/transport_run.sh`, 7 checks) requires all 27
assertions and the final banner, rejects a build that drops one byte in
transit, and keeps `loopback.c` (1061 B) and `socket_core.c` (2318 B) inside
the 8 KB budget.

**Still open:** the bottom half. A lwIP-style rewrite versus ReactOS behind
TDI/WSK is a licence-and-effort decision, not something more glue can settle;
its gate ("throughput not worse than XP") also needs the guest.

## P4.6 — the XP observation probe (code ready)

`anemos/winsock/xp_probe.c` runs a scripted set of observations inside XP and
writes a `case value` certificate. It covers exactly the questions earlier
slices deferred: legacy `inet_addr` parsing (short/hex/leading-zero/space),
`WSAStringToAddress` vs strict parsing, `connect` refusal and retry, bind
conflicts, TCP-vs-UDP port namespaces, `listen` on a datagram socket,
unconnected `send`/`shutdown`, `select` on a bad socket, listener readability
with a queued connection, accepted-socket writability, readiness after
`shutdown(SD_SEND)`, and non-blocking connect plus `SO_ERROR`.

Two facts are already established by building it: XP's `ws2_32` does **not**
export `inet_pton`/`inet_ntop` (they arrive with Vista), so those names are
not part of the XP surface — the probe uses `WSAStringToAddress` /
`WSAAddressToString`, which XP does export, and the import gate enforces it.
Our own `inet_pton`/`inet_ntop` remain a modern convenience, not a claimed XP
API.

`winsock_xp_run.sh` is the capture path: one victim (`mspaint.exe`), the
probe's certificate under `\conftest`, backup/restore on every exit path, the
same writable-VM handling as `xp_run.sh`. `conformance_compare.py` diffs the
guest certificate against the host twin case by case (changed, missing and
extra all fail). The probe's own gate (`conformance_run.sh`, 9 checks) builds
it, enforces the XP import surface, runs it under Wine only as a smoke test,
and proves the comparator can fail.

`host_probe.c` is the host twin: it answers the same 47 case names with our
socket core and loopback, and `conformance_run.sh` requires the case-name sets
to match. Running the comparator against a Wine smoke certificate (Wine is
**not** XP) already leaves nine open questions, recorded rather than "fixed":

```
inet_addr.short / .hex / .leadzero   XP-style legacy parsing accepts them; we are strict
wsastring.short/.leadzero/.hex.rc    same question through WSAStringToAddress
listen.dgram.err                     Wine says 10045 (WSAEOPNOTSUPP), we return 10022
send.unconnected.err                 Wine says 10054, we return 10057 (WSAENOTCONN)
ready.nonblock.write_ready           Wine says 0 after a refused connect, we say 1
```

These are exactly what the XP certificate will settle; no value is changed on
Wine's authority. The only remaining P4.6 step is the XP run itself.

## Blocked: P4.6

- **P4.6 — XP Winsock conformance.** Needs the VM. The host root filesystem is
  read-only and VMware cannot start (setuid-to-nobody `vmware-vmx`, no
  `/dev/vmmon`; both need root). Nothing in P4.1-P4.5 claims XP behaviour:
  XP's legacy parser quirks, `listen` on datagram sockets and the exact
  readiness edges are explicitly deferred to this probe.
- **P3's guest gates** (RAND hardening for the `rsaenh` slot, the SPI run and
  the CryptoAPI path in XP) remain open for the same reason.
- **P4.6 — XP Winsock conformance.** Needs the VM. The host root filesystem
  remounted read-only after ext4 errors, so the VMDKs cannot be patched or
  booted. Nothing in P4.1-P4.4 claims XP behaviour: XP's legacy parser quirks,
  `listen` on datagram sockets and the exact readiness edges are explicitly
  deferred to this probe.
- **P3's guest gates** (RAND hardening for the `rsaenh` slot, the SPI run and
  the CryptoAPI path in XP) remain open for the same reason.

The honest claim for P4 today is therefore: the user-space socket surface and
the resolver glue are written and host-tested against independent models, with
no XP conformance claim and no transport underneath.

## Reproducing

```sh
anemos/winsock/run.sh          # P4.1 address layer: 6/6
anemos/winsock/socket_run.sh   # P4.2/4.3 socket core: 7/7
anemos/winsock/transport_run.sh # P4.5 seam + loopback: 7/7
anemos/winsock/conformance_run.sh # P4.6 probe + comparator: 9/9
anemos/dns/resolver_run.sh      # P4.4 resolver: 8/8
```
