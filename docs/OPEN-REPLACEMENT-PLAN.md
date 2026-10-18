# Replacing Microsoft's implementations with modern, license-clean code

The goal: **every subsystem that Microsoft wrote, reimplemented on modern standards,
swapped in behind an unchanged interface, and proven equivalent against real XP.**

This is not a clean-room rewrite of Windows. It is a systematic substitution programme
with one hard rule and one honest constraint.

## The hard rule

> **Replace implementations, never interfaces.**

The 1,464 kernel exports and the ntdll syscall surface are the ABI that everything above
them is compiled against. Change either and every binary in the tree stops linking, and
so does every third-party program. So each subsystem is swapped *behind* its own API:

| Subsystem | Stable interface it hides behind | Can change freely |
|---|---|---|
| CSP / crypto | `CryptAcquireContext`, `CryptEncrypt`, … | everything — it's a pluggable provider |
| TCP/IP | Winsock, WSK, TDI | the whole stack |
| DNS resolver | `DnsQuery`, wire format RFC 1035 | the resolver |
| DHCP | RFC 2131 | the client |
| NLS / collation | `CompareString`, `LCMapString` | the tables, not the entry points |

## The honest constraint

**win32k and ntgdi are off the table.** Not because they're hard, but because rendering
*is* the appearance guarantee. Swapping in a different rasteriser changes pixels, and
"identical looks" is one of the stated goals. GDI stays.

That leaves plenty: the network stack, crypto, DNS, DHCP, NLS collation, the print
subsystem, and the utility layer.

## Why this is possible at all

Two things we now have that didn't exist before this project:

1. **A kernel that builds and boots.** The tree's own razzle toolchain under Wine builds
   all 59 kernel libraries, and the result boots a full XP installation (`docs/KERNEL-BOOTS-XP.md`).
   You can replace a DLL and boot it.

2. **An oracle.** `xp-pristine.vmdk` runs Microsoft's kernel on the same disk as
   `xp.vmdk` runs ours. Any replacement can be A/B'd against the original on identical
   hardware, and `tools/ntfspatch.py` swaps binaries host-side with no root.

That combination — rebuild, swap, boot, compare — is the whole method.

## Order of work

Chosen so each step is provable on its own and the hardest dependency comes last.

### R0 — Legal boundary (do this first, it blocks everything else)

There is currently **no LICENSE file**. Before any replacement code lands:

- `LICENSE` for our own code.
- `NOTICE` stating plainly: `Source/` is Microsoft's leaked code, is not distributed by
  us, and nothing in it is relicensed. Replacements are written against *specifications*
  and validated against XP's *observable behaviour* — never transcribed from the leak.

Without this, nobody can safely contribute, and that is fatal to a replacement effort.

### R1 — Pure algorithms (no OS surface at all)

Start where equivalence is mathematically decidable.

Candidates in-tree that implement published standards with official test vectors:
CRC-32, MD5/SHA-1/SHA-2, Base64, the NLS sort tables, the `STATUS→DOS` error mapping.

Why first: a NIST or RFC test vector is a **check that can fail**, needs no VM, no boot,
no ABI negotiation. This is where the harness gets built and trusted.

Gate: a replacement primitive produces byte-identical output to XP's on the full vector
set *and* on randomized differential testing against the real XP binary.

### R2 — Wire-format codecs (DNS, then DHCP)

DNS is the best-value target: 205 C files, a published wire format (RFC 1035), and
`dnslib` already isolates the codec from the socket layer.

Method: replace the codec, keep `DnsQuery`/`DnsWriteQuestionType` signatures, then
differential-test packet-by-packet against XP's resolver on captured traffic. Byte-exact
for well-formed input; for malformed input, XP's *specific* rejection behaviour is the
specification (this is where the leak's observable quirks are legal to use).

Gate: identical bytes out for every query type; identical error codes for every
truncation/malformation case we can generate.

### R3 — Crypto providers (fill the CSP seam with a modern engine)

`ds/security/cryptoapi` is the CSP **caller** layer. The seam is proven pluggable by the
leak itself: `ds/security/csps/` ships several interchangeable providers — CryptoFlex,
GemSafe, Infineon, smart-card CSPs — all behind the same `CryptAcquireContext` →
`CryptEncrypt` → `CryptSignHash` surface.

Notably the **software** provider (`rsaenh.dll`, RSA Base/Enhanced) is *not* in this leak.
So this is not a displacement: it is an empty slot where a modern provider belongs. Point
it at OpenSSL or libsodium, keep every CSP entry point untouched.

Highest impact of the replacements, because every caller keeps working and the security
improves rather than degrades.

Gate: XP's own crypto self-tests pass against the new provider, plus known-answer tests
for every algorithm the CSP exposes.

### R4 — The network stack (the big one)

288 C files of `net/tcpip`. Replacing it means either an lwIP-style rewrite or adopting
ReactOS's stack behind the same Winsock/TDI surface.

This is the largest and riskiest step, and it should not begin until R1–R3 have proven
the harness catches regressions.

Gate: the network conformance suite passes, and throughput is not worse than XP's.

### R5 — The legally-clean reuse layer

The tree already contains **102 files under BSD-style licenses** (BSD `pax`, `bsdlib`,
the tz database, LDAP headers, SMTP code). Those are legitimately reusable today and
should be harvested into the clean tree rather than reimplemented.

## What "done" looks like

A tree where:

- every replaced subsystem passes a conformance suite derived from XP's observed behaviour;
- no file under `Source/` has been modified to make a replacement work;
- the UI is pixel-identical (`M4` harness from `MODERNIZATION_PLAN.md`);
- and the provenance of every replacement file is either "written here" or "already
  permissive in-tree".

## The thing to be clear-eyed about

Per-subsystem replacement is achievable **because** the interfaces are stable. But the
long tail is real: crypto, DNS, DHCP and a TCP/IP stack are perhaps 5% of the NT codebase
by line count, and they are the 5% that is well-specified. The kernel proper — memory
manager, object manager, scheduler, I/O — is where the other 60M lines are, and those
have no external standard to implement against. For that, ReactOS's 25 years of clean-room
work is the only realistic source, and it is GPL/LGPL.

So the honest shape of this plan is:

| Layer | Replaced by | Realistic |
|---|---|---|
| Crypto, DNS, DHCP, wire formats, NLS tables | our own modern implementations | **yes** |
| TCP/IP | lwIP-style or ReactOS | **yes, with effort** |
| Utility layer | the 102 already-permissive files | **yes, immediately** |
| Kernel, win32k, GDI | ReactOS / nothing | **no, or not this decade** |

That is not a failure of ambition. It is the difference between a project that ships
and one that dies at 4% complete.
