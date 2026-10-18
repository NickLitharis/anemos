> **SUPERSEDED.** This document has been replaced by [`PLAN.md`](../PLAN.md).
> Kept for history only. Where it disagrees with PLAN.md, PLAN.md wins.

# Anemos — plan for a new OS inspired by Windows XP

Written after the tool/skill survey (`docs/TASK-REQUIREMENTS.md`). No implementation
code has been written yet. This is the plan to start writing it.

---

## What "rewrite into a new OS" means here

Not: modernise Microsoft's C and call it ours. That is derivative by construction
(`NOTICE.md` §3) and produces something unpublishable.

Instead: **build an OS whose observable behaviour matches XP, written fresh to 2026
standards, with XP itself as the conformance oracle.**

The leak is a *specification source and test fixture*. It never becomes the codebase.

---

## The one asset that makes this possible

Every previous attempt at "open Windows" either reimplemented blind (ReactOS, 25 years
and counting) or copied source (Wine's long-running legal exposure). This project has
something better, and it already works:

| Asset | State |
|---|---|
| A kernel built from source that boots a full XP install | ✅ verified |
| Two bootable kernels on one disk — Microsoft's and ours | ✅ `xp-pristine.vmdk` / `xp.vmdk` |
| Host-side binary swap, no root, no VMware Tools | ✅ `tools/ntfspatch.py` |
| Headless guest control (boot, screenshot, type) | ✅ `vmrun` + `vncdo` |
| Modern compiler that links with the tree's linker | ✅ clang 23 → `link.exe` → PE32+ x86-64 |

**The oracle is the project.** Without it, a rewrite is unverifiable and the effort
dies. With it, every replacement is provable.

---

## Phases

Each phase has a gate: a check that can fail. No phase starts before its predecessor's
gate passes. This is the discipline that keeps the project from dying at 4% complete.

### P0 — Foundations (no OS code yet)

| Step | Work | Gate |
|---|---|---|
| P0.1 | ✅ LICENSE / NOTICE / SCOPE / README | license boundary is explicit |
| P0.2 | ✅ OS coding rules (`docs/OS-CODING-RULES.md`) | flags compile-clean on clang 23 |
| P0.3 | **ABI boundary proof** | one clang C23 object with a `__stdcall` export and a packed struct links into the kernel build; struct offsets match MSVC's |
| P0.4 | **Conformance runner** | booting `xp.vmdk` vs `xp-pristine.vmdk`, it reports **identical** — if it reports a difference, the runner is wrong, not the kernels |

**P0.3 is the highest-risk step in the entire plan** and costs a day. If clang and the
in-tree MSVC disagree on struct layout or calling convention, everything downstream is
built on sand. Test it now, not after the DNS codec.

**P0.4 is the real work.** Not the replacements — the machine that proves they're
correct. Anyone can write a resolver; the value is the harness that says it's right.

### P1 — First replacement: pure algorithms

Start where correctness is mathematically decidable, so the harness is trusted before
it is asked to judge anything hard.

- CRC-32, MD5, SHA-1/2 — NIST and RFC test vectors
- Base64, the `STATUS→DOS` error mapping
- NLS sort/collation tables

| Gate | |
|---|---|
| Full vector sets byte-identical to XP | |
| Randomized differential testing against the real XP binary | |
| Fuzzed with libFuzzer + ASan/UBSan, no findings | |

No VM needed for most of this — it is where the team learns the workflow cheaply.

### P2 — Wire formats: DNS, then DHCP

DNS first: 205 C files in the tree, published wire format (RFC 1035), and the codec is
already separated from the socket layer.

| Gate | |
|---|---|
| Identical bytes out for every query type | |
| Identical error codes for every truncation/malformation case we generate | |
| Runs inside XP against a real resolver, byte-compared on captured traffic | |

### P3 — Crypto: fill the CSP seam

The leak proves the seam is pluggable — `ds/security/csps/` ships CryptoFlex, GemSafe and
Infineon providers behind one interface. The software provider (`rsaenh.dll`) is **absent
from the leak**, so this is an empty slot, not a displacement: an OpenSSL/libsodium-backed
CSP goes where Microsoft's used to be.

| Gate | |
|---|---|
| XP's own crypto self-tests pass against the new provider | |
| Known-answer tests for every algorithm the CSP exposes | |
| No calling code changed | |

### P4 — Network stack

288 C files of `net/tcpip`. Largest and riskiest; deliberately last of the replacements,
after the harness has caught real regressions three times.

| Gate | |
|---|---|
| Winsock conformance suite passes | |
| Throughput and latency not worse than XP's | |
| No behaviour change observable to existing applications | |

### P5 — Kernel proper — *out of scope for this plan*

Memory manager, object manager, scheduler, I/O. ~60M lines, **no external specification**
to implement against, and no way to differential-test bit-identical internal state.

The honest options are: adopt ReactOS's clean-room kernel (GPL/LGPL — a license change
for the project), or accept that this layer stays Microsoft's. Deciding that is a
deliberate fork in the road, and it should be decided after P2 proves the method, not now.

---

## What gets built, concretely

```
anemos/
├── spec/            behaviour specs derived from XP (our work, MIT)
│   ├── dns/         packet captures + expected responses
│   ├── crypto/      NIST vectors + observed CSP behaviour
│   └── errors/      STATUS->DOS mapping, observed
├── harness/         conformance runner (our work, MIT)
│   ├── vm.py        boot / snapshot / patch-a-binary
│   ├── capture.py   collect observations from the guest
│   └── compare.py   diff subject vs control, report
├── src/             THE OS — C23, written to docs/OS-CODING-RULES.md
│   └── <subsystem>/ one directory per replacement, extern "C" boundary
└── docs/            rules, plans, measurements
```

The `spec/` directory is the crown jewel: it is legally clean (observations, not code),
it is the thing no other project has, and it outlives any particular implementation.

---

## What I need before starting P0.3

1. **Confirm the project name** — I used "Anemos" (Greek: wind; XP was "Whistler"). Change
   it now, it is in three files.
2. **Confirm the license** — MIT is in place for our code. If you later adopt ReactOS
   kernel code, the project becomes GPL/LGPL and that is contagious. Decide before
   depending on it.
3. **Accept that the kernel proper is P5-and-maybe-never.** The plan is honest about this
   rather than promising a full OS by reimplementing 60M lines.

---

## Order I recommend starting in

1. **P0.3 — ABI proof.** One day. De-risks everything.
2. **P0.4 — conformance runner**, validated on the two kernels we already have.
3. **P1 — first replacement**, to exercise the runner for real.

Say go and I start with P0.3.
