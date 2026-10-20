# Anemos

An open-source operating system inspired by Windows XP: new code, written to
2026 standards, validated for behavioural compatibility against XP itself.

Not a fork. Not a leaked release. New code, written against public
specifications, with XP's observed behaviour as the oracle.

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
**no Anemos kernel yet**; there is a verified way to build and judge one, and a
growing set of subsystems that already pass it.

| Capability | State | Evidence |
|---|---|---|
| Build XP's kernel from source | ✅ 59 libraries, 1,464 exports | `docs/KERNEL-BUILDS.md` |
| Boot it into a full XP install | ✅ Luna desktop, responsive `cmd` | `docs/KERNEL-BOOTS-XP.md` |
| Build 21/23 kernel subsystems for **AMD64** | ✅ | `docs/X64-STATUS.md` |
| **G1 — ABI compatibility proof** | ✅ 15/15 | `docs/G1-ABI-PROOF.md` |
| **G2 — conformance runner** | ✅ IDENTICAL on the negative control | `docs/G2-CONFORMANCE-RUNNER.md` |
| **P1 — pure algorithms** | ✅ 6/6 units | `anemos/{md5,sha1,base64,crc32,nls,statusdos}/` |
| **P2 — DNS codec + client** | ✅ 24/24 | `docs/P2-DNS.md`, `docs/P2-DNS-CLIENT.md` |
| **P2 — DHCP codec + client** | ✅ 24/24 | `docs/P2-DHCP.md`, `docs/P2-DHCP-CLIENT.md` |
| **P3 — crypto engine** | ✅ OpenSSL 3.5.7 for i686 Windows, 6/6 KATs | `docs/P3-CRYPTO.md` |
| **P3.1 — CSP SPI skeleton** | ✅ 7/7, 23 + 2 exports | `docs/P3-CSP-SPI.md` |
| P3.2+ — keys, RSA, CryptoAPI path | ⏳ next | — |
| P4 — network stack | ❌ not started | — |
| P5 — kernel proper | ❌ out of scope | see `PLAN.md` |

**P2 is complete.** The DNS and DHCP codecs, their clients, and a deterministic
DHCP state machine are written, documented, and proved against independent
second implementations. P3 has started: the crypto engine is cross-built for the
destination ABI, and the CryptoSPI surface is exported and exercised.

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
tags           ctags index of the XP source
Source         symlink to the XP source outside this repository (gitignored)
```

`Source/` is **not** part of this project. It is leaked Microsoft code, kept
outside the repository and reachable only through a local symlink for reading
and as a test oracle. It is never modified and never shipped. See `NOTICE.md`
and `SCOPE.md`.

## Verify it

Every unit is a self-contained gate; none needs a VM except the two original
oracle gates.

```sh
anemos/statusdos/run.sh     # STATUS->DOS, proved against XP for 2^32 statuses (14/14)
anemos/dns/run.sh           # DNS codec + client (24/24)
anemos/dhcp/run.sh          # DHCP codec + client (24/24)
anemos/crypto/run.sh        # OpenSSL KATs for the Windows target (6/6)
anemos/csp/run.sh           # CSP SPI, loaded and called directly (7/7)
tools/abi/run.sh            # G1, clang vs MSVC (15/15)
```

`anemos/crypto/build-openssl.sh` builds the pinned OpenSSL 3.5.7 for
`i686-w64-mingw32` once (outside the repo, under `~/.local/opt`). `csp` and
`crypto` fail with a clear message until it has been run.

The G2 runner and the kernel boot need the two VMDKs and the legacy toolchain;
those are environment assets, not part of this repository.

## Scope, and the honest limit

- **P1 — pure algorithms.** CRC-32, MD5, SHA-1, Base64, NLS tables, the
  `STATUS→DOS` map. Done.
- **P2 — wire formats.** DNS (RFC 1035) and DHCP (RFC 2131/2132), each with a
  codec, a client, and an independent oracle. Done.
- **P3 — crypto.** A modern engine (OpenSSL 3.5.7) behind the empty `rsaenh`
  slot. The engine and the provider SPI are proved; key operations and the
  CryptoAPI path remain.
- **P4 — network stack.** ~288 C files; the largest replacement, deliberately
  last.
- **P5 — the kernel proper.** Memory manager, object manager, scheduler, I/O:
  ~60M lines with no external specification and no way to differential-test
  bit-identical internal state. Out of scope. The honest options are adopting
  ReactOS's clean-room kernel (GPL/LGPL, which changes this project's licence)
  or accepting that layer stays Microsoft's.

**win32k and ntgdi are also out.** Rendering *is* the appearance guarantee; a
different rasteriser means different pixels, and identical looks is a stated
goal. GDI stays.

## Two measured P3 constraints

Both are documented rather than discovered late:

- **XP verifies provider image signatures** before calling `CPAcquireContext`.
  An unsigned third-party DLL cannot be loaded through CryptoAPI on stock XP.
  The provider's SPI is therefore proved directly (`LoadLibrary` + entry
  points); the CryptoAPI path needs a declared harness decision.
- **OpenSSL's Windows RAND uses CryptoAPI**, which would re-enter the provider
  if it replaced `rsaenh.dll`. The CSP seeds from `SystemFunction036`
  (`RtlGenRandom`), which is independent of the CSP, and must prove no
  recursion when installed.

## Read next

- **`PLAN.md`** — the authoritative plan and the measured state behind every ✅
- `SCOPE.md` / `NOTICE.md` / `LICENSE` — what is ours, what is Microsoft's
- `docs/OS-CODING-RULES.md` — 17 rules, each naming the failure it prevents
- `docs/SIZE-POLICY.md` — measured size levers and budgets
- `docs/TOOLCHAIN-POLICY.md` — clang/lld 23.1.3, pinned
- `docs/P2-DNS.md`, `docs/P2-DHCP.md`, `docs/P3-CRYPTO.md` — the unit gates

## License

MIT for this project's own code, exactly as listed in `SCOPE.md`. Nothing in
`Source/` is licensed to you; publishing it is copyright infringement.
