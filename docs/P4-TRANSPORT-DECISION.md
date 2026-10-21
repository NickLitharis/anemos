# P4.5 — the transport decision

The user-space surface is done and gated: address layer, socket lifecycle,
readiness/data path, resolver glue, loopback transport, and the XP Winsock
certificate reproduced 47/47. The one thing P4 has not chosen is what sits
under the transport seam. This memo records the options and a recommendation;
it deliberately does not vendor a stack.

## What the seam already fixes

`anemos/winsock/loopback.*` implements the request/event interface the socket
core expects. A real provider only has to do the same work with real packets:
`open`, `bind`, `listen`, `connect`, `accept`, `send`, `shutdown`, `close`,
`refuse` in; `connected`, `refused`, `incoming`, `readable`, `writable`, `eof`,
`reset` out. Nothing above that seam changes if the bottom half is replaced.

## The options

1. **lwIP-style clean implementation** (lwIP itself is BSD-3).
   - Licence: compatible with this project's MIT.
   - Effort: highest; needs a TCP/IP implementation plus the XP transport glue.
   - Control: complete; behaviour can be shaped to the XP certificate.
2. **Adopt ReactOS's network stack** (GPL/LGPL).
   - Licence: forces the combined deliverable to GPL/LGPL; this project is MIT.
   - Effort: lower, but the integration surface (TDI/WSK, NDIS) is still real.
   - Control: follows ReactOS's own XP-compatibility choices, which are not
     automatically this project's oracle.
3. **Keep the user-space surface and stop** (recommended until the kernel
   question is settled).
   - Licence: unchanged.
   - Effort: none beyond what is already gated.
   - Cost: no actual packet transport; the resolver and socket API run over the
     loopback seam only.

## The constraint the decision must face

XP's network stack is `tcpip.sys`, a **kernel driver**. Replacing it means
writing kernel-mode code — precisely what P5 excludes ("memory manager, object
manager, scheduler, I/O ... out of scope"). A user-space library cannot occupy
that slot; a user-space proxy would be a different architecture with different
claims and different performance, and could not be called a tcpip replacement.

So P4.5 is not only "lwIP versus ReactOS"; it is also a **scope decision**:
either the P5 boundary is renegotiated to allow a kernel transport driver, or
P4 ships the user-space surface without a packet transport.

## Recommendation

- Keep MIT and do **not** adopt ReactOS.
- Do **not** vendor lwIP yet: without a decision to write kernel-mode code it
  would be dead weight, and its integration is the hard part, not the library.
- Record P4.5 as blocked on the P5 scope decision. If that boundary moves, the
  preferred route is lwIP (BSD-3) behind the existing seam, with the XP
  certificate as the behavioural oracle and throughput measured in the guest.

## What this means for the P4 claim

P4's gate is "Winsock conformance; throughput not worse than XP". The
conformance half is now evidenced for the observed surface (47/47). The
throughput half has no subject yet and cannot be claimed. That is the honest
state: a tested user-space Winsock surface, not a tested network stack.
