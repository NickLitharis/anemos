# P2 DHCP client: a state machine you can diff

`anemos/dhcp/dhcp_client.{h,c}` is the DHCP client state machine that sits on
the codec. It owns no socket and no clock: the caller supplies the current time
in milliseconds, delivers received frames, and gets back at most one frame to
send. That is what makes the machine testable without a network, a VM, or a
sleep.

There is **no Microsoft source here**. RFC 2131 §4.4 is the specification;
XP's client behaviour is not claimed.

## States and API

```
INIT -> SELECTING -> REQUESTING -> BOUND -> RENEWING -> REBINDING
           ^            |            |          |           |
           +------------+------------+----------+-----------+   (NAK/expiry)
```

```c
AnemDhcpStatus anem_dhcp_client_init(AnemDhcpClient *cl, uint32_t seed,
                                     const uint8_t chaddr[16], uint8_t hlen);
AnemDhcpStatus anem_dhcp_client_start(AnemDhcpClient *cl, uint64_t now_ms,
                                      void *out, size_t cap, size_t *out_len);
AnemDhcpStatus anem_dhcp_client_receive(AnemDhcpClient *cl, uint64_t now_ms,
                                        const void *wire, size_t wire_len,
                                        void *out, size_t cap, size_t *out_len);
AnemDhcpStatus anem_dhcp_client_tick(AnemDhcpClient *cl, uint64_t now_ms,
                                     void *out, size_t cap, size_t *out_len);
```

`*out_len == 0` means "send nothing". A received frame that is irrelevant —
wrong opcode, xid or chaddr, or an unknown message type — is ignored (`OK`,
nothing to send). A frame that does not parse returns the codec's status, which
lets the caller count malformed traffic without the state machine guessing.

## What it sends, and when

| State | Packet | Flags | Options |
|---|---|---|---|
| start / restart | DISCOVER | broadcast | 53, 55, 61 |
| SELECTING + OFFER | REQUEST | broadcast | 53, 50, 54, 61 |
| REQUESTING retransmit | REQUEST | broadcast | 53, 50, 54, 61 |
| BOUND -> RENEWING | REQUEST | unicast | 53, 61, `ciaddr` = lease |
| RENEWING retransmit | REQUEST | unicast | 53, 61 |
| RENEWING -> REBINDING | REQUEST | broadcast | 53, 61 |
| REBINDING retransmit | REQUEST | broadcast | 53, 61 |
| lease expiry / NAK | DISCOVER | broadcast | 53, 55, 61 |

The xid is generated once per transaction and kept across retransmissions. A
restart (NAK or expiry) generates a new one.

## Timers

- DISCOVER/REQUEST retransmit: 4 s, doubling to a 64 s cap.
- At T1 the client moves to RENEWING and retransmits at **half the time
  remaining to T2**, with a 60 s floor (RFC 2131 §4.4.5).
- At T2 it moves to REBINDING and retransmits at half the remaining lease,
  with the same floor.
- At lease expiry it restarts with a new DISCOVER.
- T1 defaults to `lease/2`, T2 to `7*lease/8`. Server-provided T1/T2 are used
  only when `0 < T1 < T2 < lease`; otherwise the defaults win. An ACK without
  a usable lease is ignored rather than half-applied.

## Two deliberate deviations, both visible

**No jitter.** RFC 2131 recommends randomised retransmission delays. A random
client cannot be diffed against a second implementation, so the delays here are
exact. A caller that wants RFC-like spread can schedule `tick()` with jitter;
the state machine does not hide one.

**xid from a caller-seeded LCG.** `seed = seed * 1664525 + 1013904223` (mod
2^32). This is not a security RNG and is not claimed to be one; it exists so a
trace is reproducible. Seed it from entropy in production and the transaction
IDs are as unpredictable as the seed.

## Verification

`anemos/dhcp/run.sh` — 24 checks in total for the codec + client, all able to
fail. The client-specific ones:

1. **Scenario suite, i686 PE under Wine.** Every emitted packet is parsed with
   the codec and checked for type, xid, `ciaddr`, broadcast flag and option
   contents; every deadline is checked as an absolute number. The scenarios
   cover: start, backoff, OFFER -> REQUEST, ACK -> BOUND, T1 renewal, renewal
   retransmission, renewal ACK, T2 rebinding, rebinding ACK, NAK restart, lease
   expiry restart, wrong xid/chaddr/opcode/server/address being ignored,
   malformed input, short output buffers, and every NULL contract.
2. **A wrong retransmission base must fail the suite.** The negative build uses
   5 s instead of 4 s; the first deadline check rejects it.
3. **Trace differential against an independent Python model.** Six traces,
   130 operations, 72 emitted packets, covering all six states (INIT 6,
   SELECTING 42, REQUESTING 36, BOUND 20, RENEWING 22, REBINDING 4). After
   every operation both implementations must print the same state, xid,
   addresses, lease, T1/T2, every absolute deadline, attempt count, and the
   exact packet bytes. The C side runs under ASan and UBSan, so a bad pointer
   in a rejected frame is a failure, not a silent pass. The weakened client
   must disagree on the same traces.
4. **Freestanding object + size budget.** `dhcp_client.c` compiles for
   `i686-windows-msvc`; its `.text` is **2,613 bytes** (`dhcp.c` is 2,003), both
   inside the 8 KB per-TU budget.

The endianness bug this suite caught is worth recording: the option builder
copies bytes verbatim, so passing `&uint32_t` produced little-endian option
payloads. That is invisible on loopback and wrong on the wire. The fix was
`anem_dhcp_build_option_u8/u16/u32`, which encode network order; the state
machine and the test server both use them now.

Reproduce:

```sh
anemos/dhcp/run.sh
```

## Limits

- **No INIT-REBOOT.** A client that remembers its address across a reboot
  should request it directly; this machine always starts with DISCOVER.
- **No DECLINE or RELEASE.** The API can emit them, but no state triggers
  them. Decline-on-conflict needs ARP, and RELEASE needs a shutdown hook; both
  belong with the socket layer.
- **No persistence.** The lease is in the struct, not on disk.
- **No transport decisions.** Whether a packet goes to the broadcast address or
  unicast is the caller's job; the state machine only chooses the broadcast
  *flag* per RFC.
- **No option overload.** Inherited from the codec: options in `sname`/`file`
  are not read.
- **No randomised delays.** See above.

## What this proves and what it does not

It proves the state machine is deterministic, its timers fire at the documented
instants, its packets are exactly the bytes both implementations expect, and
malformed or irrelevant traffic cannot move it. The trace differential is a
real second implementation, not a recording: it derives its output by running
the same rules over the same inputs.

It does **not** prove XP's DHCP client would take the same decisions. That
requires the guest. When the socket path exists (P4), the same traces can be
replayed against XP's NAK/timer behaviour; until then XP compatibility is not
claimed.
