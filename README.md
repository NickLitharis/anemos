# Anemos

An open-source operating system inspired by Windows XP: new code, written to
2026 standards, validated for behavioural compatibility against XP itself.

Not a fork. Not a leaked release. New code, written against public
specifications, with XP's observed behaviour as the oracle.

Public repository: <https://github.com/NickLitharis/anemos>

## The idea in one paragraph

Windows XP is the compatibility target, not the codebase. The leaked XP source
is a specification and a test fixture: it is not in this repository, and no
replacement is derived from it. Where our behaviour and XP's disagree, XP is
right — the disagreement is recorded, not "fixed". The work is deliberately
front-loaded on the layer that has published standards (RFCs, NIST vectors,
documented ABIs) and can therefore be proved: DNS, DHCP, crypto, and the
protocol edges of the network stack. That is roughly 5% of NT by line count, and
it is the 5% that can be verified without a clean-room rewrite of a kernel that
has no external specification.

## Status

The method is proven and the first replacements are written and gated. There is
**no Anemos kernel yet**: there is a verified way to build and judge one, and a growing
set of subsystems that already pass it.

- **P0–P3 complete.** Both gates passed (G1 ABI, G2 conformance), P1's algorithms and
  the exhaustive `STATUS→DOS` map are proved, P2's DNS and DHCP units are gated against
  independent implementations, and P3's crypto engine and CSP SPI surface run
  identically in the host harness and inside XP.
- **P4 written, gated where it can be.** XP Winsock conformance is 47/47, the socket
  core and resolver are proved against models, and lwIP (BSD-3) is the adopted stack;
  the transport provider is the open decision.
- **Kernel network driver in progress.** K1 (loadable driver) and K2 (lwIP in the
  kernel) passed in the guest, K3 binds to NDIS, and its frame path builds with the
  guest run pending.

Two things sit outside that list. XP's own kernel can be built from source and boots a
full install — that is Microsoft's code, used as the oracle, not the deliverable. And
P5's kernel proper is out of scope, with the network transport as its only exception.

Per-milestone detail, dates and evidence are in **`CHANGELOG.md`**; `PLAN.md` is the
authoritative statement of scope and measured status.

## Phase tags

Each completed phase is an annotated git tag — `phase-p0` through `phase-p3.4` — so a
phase can be reviewed, diffed or pushed on its own. `CHANGELOG.md` records what each tag
contains and when; `PHASES.md` defines the boundaries and what belongs to each phase.

## How correctness is established

Two rules run through the whole project:

1. **A check that cannot fail is not a check.** Every gate has a negative
   control that must fail: a deliberately weakened parser, a wrong digest, a
   corrupted step in an algorithm.
2. **The implementation is never its own oracle.** Wire-format units are diffed
   against a second implementation written independently from the same RFC,
   over a corpus of valid, truncated, and mutated inputs. The `STATUS→DOS` map
   was proved against live XP for all 2^32 statuses.

The two gates that had to pass before any replacement:

- **G1 — ABI.** clang 23 and the tree's MSVC 13.00 agree on struct layout,
  calling convention and name mangling. If this had failed, every replacement
  would silently corrupt structures.
- **G2 — conformance runner.** Boots a VM, runs tests inside XP, collects
  observations, and diffs subject against control. Pointed at `xp.vmdk` and
  `xp-pristine.vmdk` — the same disk, two kernels — it must report identical.
  That negative control is what makes it trustworthy.

## Layout

```
anemos/        the replacements: md5, sha1, base64, crc32, nls, statusdos,
               dns, dhcp, crypto, csp
docs/          rules, policies, measurements, and one document per gate/unit
harness/       razzle wrappers for the legacy build, and the conformance runner
tools/         ntfspatch.py, code generation, the ABI proof, toolchain.lock
kernel/ boot/  earlier freestanding experiments, kept as verified history
Source         symlink to the XP source outside this repository (gitignored)
```

`Source/` is **not** part of this project. It is leaked Microsoft code, kept
outside the repository and reachable only through a local symlink for reading
and as a test oracle. It is never modified and never shipped. See `NOTICE.md`
and `SCOPE.md`.

Microsoft-derived build evidence (`dist/`, `docs/kernel-build/`,
`docs/razzle-evidence/`, the generated codemap) and the ctags index are kept
locally in `~/anemos-private/`. They are not part of this repository or its
history and are not redistributed.

## Verify it

Every unit is a self-contained gate; none needs a VM except the two original
oracle gates.

```sh
anemos/statusdos/run.sh     # STATUS->DOS, proved against XP for 2^32 statuses (14/14)
anemos/dns/run.sh           # DNS codec + client (24/24)
anemos/dhcp/run.sh          # DHCP codec + client (24/24)
anemos/crypto/run.sh        # OpenSSL KATs + XP import gate (7/7)
anemos/csp/run.sh           # CSP SPI + XP import gate (11/11)
tools/abi/run.sh            # G1, clang vs MSVC (15/15)
```

`anemos/crypto/build-openssl.sh` builds the pinned OpenSSL 3.5.7 for
`i686-w64-mingw32` once (outside the repo, under `~/.local/opt`). `csp` and
`crypto` fail with a clear message until it has been run.

The G2 runner and the kernel boot need the two VMDKs and the legacy toolchain;
those are environment assets, not part of this repository. The Microsoft-derived
binaries and logs those checks produce are kept locally in `~/anemos-private/`,
not published here.

## Scope, and the honest limit

- **P1 — pure algorithms.** CRC-32, MD5, SHA-1, Base64, NLS tables, the
  `STATUS→DOS` map. Done.
- **P2 — wire formats.** DNS (RFC 1035) and DHCP (RFC 2131/2132), each with a
  codec, a client, and an independent oracle. Done.
- **P3 — crypto.** A modern engine (OpenSSL 3.5.7) behind the empty `rsaenh`
  slot. The engine, the provider SPI, AES (one-shot and streaming) and RSA
  sign/verify are proved. The CryptoAPI path is proved as plumbing under Wine, including the
  little-endian signature convention, and the in-guest SPI run is identical to
  the host. OpenSSL is hardened against CryptoAPI RAND re-entry; stock XP
  still verifies the provider image signature; the flag experiment confirms
  every route fails with `NTE_BAD_SIGNATURE`, so that path is documented as
  unclaimable rather than patched.
- **P4 — network stack.** ~288 C files; the largest replacement, deliberately
  last. Started with the host-testable Winsock address layer (P4.1). The
  architecture question is settled — lwIP (BSD-3) was adopted and ReactOS
  rejected — and the remaining choice is the transport provider, blocked on the
  P5 scope decision (`docs/P4-TRANSPORT-DECISION.md`).
- **P5 — the kernel proper, with one exception.** The network stack now needs a
  kernel driver and is in scope (`docs/P5-NETWORK-KERNEL.md`); its first
  loadable driver passed in XP on 2026-10-22, lwIP runs inside it with an
  in-kernel loopback self-test, and the protocol is installed through netcfg
  and bound by NDIS. The frame path (receive plus ARP transmit) builds as a
  68,608-byte image with its kernel imports resolved; running it in the guest
  is the next step. Memory manager,
  object manager, scheduler, I/O:
  ~60M lines with no external specification and no way to differential-test
  bit-identical internal state. Out of scope. The honest options are adopting
  ReactOS's clean-room kernel (GPL/LGPL, which changes this project's licence)
  or accepting that layer stays Microsoft's.

**win32k and ntgdi are also out.** Rendering *is* the appearance guarantee; a
different rasteriser means different pixels, and identical looks is a stated
goal. GDI stays.

## Three measured P3 constraints

All are documented rather than discovered late:

- **XP verifies provider image signatures** before calling `CPAcquireContext`.
  An unsigned third-party DLL cannot be loaded through CryptoAPI on stock XP.
  The provider's SPI is therefore proved directly (`LoadLibrary` + entry
  points); the CryptoAPI path needs a declared harness decision.
- **OpenSSL's Windows RAND uses CryptoAPI**, which would re-enter the provider
  if it replaced `rsaenh.dll`. The CSP seeds from `SystemFunction036`
  (`RtlGenRandom`), which is independent of the CSP, and must prove no
  recursion when installed.
- **Wine is not XP.** The first engine build passed every KAT under Wine but
  imported ten KERNEL32 functions that XP SP1 does not export: SRW locks,
  condition variables, and a fiber API. The engine is now built with
  `no-threads no-async no-thread-pool`, and both crypto and CSP gates compare
  the PE's imports against the export tables of a pristine XP install.

## Read next

- **`PLAN.md`** — the authoritative plan and the measured state behind every ✅
- **`CHANGELOG.md`** — what each phase tag contains, with dates
- `PHASES.md` — phase boundaries, tags, and which code belongs to each phase
- `SCOPE.md` / `NOTICE.md` / `LICENSE` — what is ours, what is Microsoft's
- `docs/OS-CODING-RULES.md` — 17 rules, each naming the failure it prevents
- `docs/SIZE-POLICY.md` — measured size levers and budgets
- `docs/TOOLCHAIN-POLICY.md` — clang/lld 23.1.3, pinned
- `docs/P2-DNS.md`, `docs/P2-DHCP.md`, `docs/P3-CRYPTO.md` — the unit gates

## License

MIT for this project's own code, exactly as listed in `SCOPE.md`. Nothing in
`Source/` is licensed to you; publishing it is copyright infringement.
