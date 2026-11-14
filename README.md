# Anemos

An open-source operating system with an independently implemented kernel,
inspired by Windows XP and judged by compatibility with XP's observable behavior.

Anemos's kernel is not a fork of Microsoft's. XP is the comparison target, not
the implementation. The repository contains no Microsoft-origin kernel or boot
implementation; those historical experiments are removed from the checkout and
local Git history. See `SCOPE.md` and `NOTICE.md` for the provenance boundary.

Public repository: <https://github.com/NickLitharis/anemos>

## The idea in one paragraph

Windows XP is the compatibility target, not the codebase. The project must boot
and run its own kernel. Existing P0–P4 libraries and test infrastructure are
useful building blocks, but they do not constitute a kernel. New kernel code
must have recorded provenance and must not be transliterated from Microsoft
source.

## Status

The compatibility method and several libraries are proven. **AK3 now boots an
independent Anemos kernel with exception handling, a hardware timer, physical
page allocation/mapping, and a bounded kernel heap.**

- **P0–P4:** host-side conformance infrastructure, independently implemented
  algorithms/protocols, and XP compatibility probes remain supporting work.
- **AK1–AK3 kernel:** `./boot/build.sh` builds a C23 x86_64 ELF, packages a
  Limine BIOS ISO, and checks page allocation/reuse, virtual aliases, heap
  split/coalescing/exhaustion, timer interrupts, and divide-by-zero panic output
  over QEMU serial.
- **Current limit:** no scheduler, process address spaces, user-mode execution,
  or device services yet.
- **`anemos/net/kernel/`:** an XP-loaded NDIS driver experiment, not an
  independent kernel. Its latest commit records a boot-time `0xD1` fault; frame
  send/receive is unproven.

The next kernel milestone is AK4: execution, context switching, scheduling,
and synchronization. See **`PLAN.md`** for the sequence and evidence.

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

anemos/        independent libraries and compatibility layers
kernel/        original C23 kernel, x86_64 exception/IRQ handling and PIT
boot/          Limine BIOS build configuration and QEMU smoke gate
docs/          engineering rules, measurements, and milestone evidence
harness/       XP build wrappers and behavioral conformance runner
Source/        external, gitignored XP source reference/oracle
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

`./boot/build.sh` is the AK3 QEMU acceptance gate: it builds the ISO and checks
the memory allocator, virtual mapping, bounded heap, timer, and panic serial
messages.

```sh
./boot/build.sh              # AK3: build, boot QEMU, verify memory, timer and panic
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

The G2 runner and the **XP oracle boot** need the two VMDKs and legacy toolchain;
those are environment assets, not part of this repository. This is not an
Anemos-kernel boot. Microsoft-derived binaries and logs are kept locally in
`~/anemos-private/`, not published here.

## Scope and current limit

The goal is an independently bootable XP-inspired kernel. AK3 proves the boot
path, CPU exception reporting, timer interrupts, physical page allocation,
4-level page mapping, and a bounded kernel heap. Next: scheduling, processes and
system calls, kernel services, I/O/drivers, and a defined XP-compatible ABI.
Networking is one subsystem.

Existing P1–P4 work remains reusable: independently implemented algorithms,
DNS/DHCP, crypto experiments, Winsock models and XP observation gates. The XP
NDIS/TDI driver work is a separate compatibility experiment; it depends on
Microsoft's kernel and cannot stand in for Anemos's kernel.

**Release boundary:** the XP-derived experiments and outputs formerly tracked
under `kernel/` and `boot/` were removed from this checkout and local Git refs.
The cleaned history has been force-pushed to the public remote; old clones
must be recreated or carefully reset. The current AK1–AK3 files in those paths
are original code. The ignored `Source/` symlink is external and is not part
of the repository.

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
