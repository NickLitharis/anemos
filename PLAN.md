# Anemos — the plan

**This document supersedes `MODERNIZATION_PLAN.md`, `docs/OPEN-REPLACEMENT-PLAN.md` and
`docs/ANEMOS-PLAN.md`.** Those are kept for history; where they disagree with this, this
wins. They used three incompatible numbering schemes (M0-M4, R0-R5, P0-P5) for the same
work, which is a reliable way to lose track of what is done.

---

## The goal, stated once

Build an operating system inspired by Windows XP: **new code, written to 2026 standards,
validated for behavioural compatibility against XP itself.**

Three constraints follow, and they are not negotiable:

1. **Legal.** No Microsoft source in the deliverable. The leak is a specification and a
   test oracle only (`NOTICE.md`).
2. **Standards.** C23 / C++23, clang 23.1.3, `docs/OS-CODING-RULES.md` (17 rules),
   `docs/SIZE-POLICY.md`.
3. **Compatibility.** XP is the oracle. Where our behaviour differs, XP is right.

---

## Where we actually are

Not a roadmap — measured state as of today.

| Capability | Status | Evidence |
|---|---|---|
| Build XP's kernel from source | ✅ | 59 libraries, 1,464 exports matching Microsoft's |
| Boot it into a full XP install | ✅ | `docs/KERNEL-BOOTS-XP.md` |
| Build 21/23 kernel subsystems for **AMD64** | ✅ | `docs/X64-STATUS.md` |
| Modern compiler → legacy libs → working PE | ✅ | clang 23.1.3 + `lld-link` on real `ntosrtl.lib` |
| Two bootable kernels, one disk (the oracle) | ✅ | `xp-pristine.vmdk` / `xp.vmdk` |
| Host-side binary swap, no root | ✅ | `tools/ntfspatch.py` |
| Legal boundary | ✅ | `LICENSE`, `NOTICE.md`, `SCOPE.md` |
| Coding standards | ✅ | `docs/OS-CODING-RULES.md` |
| **Conformance runner** | ❌ **not built** | — |
| **ABI compatibility proof** | ❌ **not done** | — |
| **Any Anemos code** | ❌ **none** | — |

**There is no Anemos yet.** There is a proven method for producing and verifying one.

---

## The two gates

Everything else waits behind these. Both are small, both are cheap, and skipping either
means building on sand.

### G1 — ABI compatibility proof  *(~1 day)*

**Question:** do clang 23 and the tree's MSVC 13.00 agree on struct layout, calling
conventions and name mangling?

**Method:** one clang-built C23 object exporting a `__stdcall` function taking a packed
struct; link against a real kernel library; call it from something that already builds;
compare every field offset against the MSVC-computed values.

**Gate:** offsets and call ABI match exactly. **If this fails, nothing downstream is
trustworthy** — every replacement would silently corrupt structures.

This is the highest-risk item in the entire project and it costs one day. Do it first.

### G2 — Conformance runner  *(~1 week)*

**Question:** can we prove a replacement behaves like XP?

**Method:** `harness/` gains the ability to boot a VM, install a subject binary, run N
tests inside XP, collect observations (return codes, packets, registry state, screenshots),
and diff against the control.

**Gate:** the runner, pointed at `xp.vmdk` and `xp-pristine.vmdk` — **the same kernel data,
two disk images** — must report **identical**. If it reports a difference, the runner is
wrong, not the kernels. That negative control is what makes it trustworthy.

**This is the real work of the project.** Writing a DNS codec is a few days; knowing it is
*correct* is the hard part, and it is the thing that outlives any implementation.

---

## Phases

Each phase begins only when the previous gate passes.

| Phase | Work | Gate |
|---|---|---|
| **P0** | G1, G2 | ABI matches; runner reports identical on the negative control |
| **P1** | First replacement: pure algorithms — CRC-32, MD5/SHA, Base64, `STATUS→DOS` map, NLS tables | byte-identical on NIST/RFC vectors + randomised differential vs XP; fuzzed clean |
| **P2** | Wire formats: DNS codec (205 C files, RFC 1035), then DHCP | identical bytes out, identical error codes on malformed input |
| **P3** | Crypto: an OpenSSL-backed CSP in the empty `rsaenh` slot | XP's own crypto self-tests pass; known-answer tests for every exposed algorithm |
| **P4** | Network stack (288 C files) — largest, deliberately last | Winsock conformance; throughput not worse |
| **P5** | Kernel proper | **Out of scope.** See below. |

P1 comes before P2 because it needs no VM, so the runner gets exercised cheaply before it
is asked to judge anything hard.

---

## What is deliberately out of scope

**win32k and ntgdi.** Rendering *is* the appearance guarantee. A different rasteriser
means different pixels, and "identical looks" is a stated goal. GDI stays.

**The kernel proper (P5).** Memory manager, object manager, scheduler, I/O — ~60M lines
with **no external specification** to implement against and no way to differential-test
bit-identical internal state. The honest options are adopting ReactOS's clean-room kernel
(GPL/LGPL, which changes this project's licence) or accepting that layer stays Microsoft's.
Decide after P2 proves the method, not now.

By line count, P1–P4 are roughly **5% of NT**. They are also the 5% that has published
standards. That is the honest scope of "an open OS": a well-specified shell over a kernel
nobody has legally rewritten.

---

## Working rules

- **XP wins every disagreement.** Record the conflict; do not "fix" XP.
- **`Source/` is read-only.** Never modify it to make a replacement work. Build side
  effects (regenerated `bugcodes`, rewritten `.lib`) are reverted, not committed.
- **`-Z`, never `-cZ`** for legacy builds — `-c` wipes `obj/` and destroys tracked
  prebuilt objects whose source is missing.
- **Measure, then claim.** Every flag in `SIZE-POLICY.md` was measured; two turned out to
  be folklore and are documented as such.
- **A check that cannot fail is not a check.** The runner's own negative control is the
  example.
- **Size is a budget**, not an afterthought: ≤64 KB `.text` per subsystem, `-Oz`.

---

## Next three actions

1. **G1 — ABI proof.** One day. Say go and I start.
2. **G2 — conformance runner skeleton**, validated on the two kernels we already have.
3. **P1 — first replacement**, to exercise the runner on something real.

Nothing else should be started before G1 and G2 pass. Everything after them is
substitution work behind stable interfaces, which is tractable precisely because the
interfaces do not move.

---

## Reference index

| Document | Purpose |
|---|---|
| `SCOPE.md`, `NOTICE.md`, `LICENSE` | What is ours; what is Microsoft's |
| `docs/OS-CODING-RULES.md` | 17 rules, each naming its failure mode |
| `docs/SIZE-POLICY.md` | Measured size levers and budgets |
| `docs/TOOLCHAIN-POLICY.md` | clang/lld 23.1.3, pinned in `tools/toolchain.lock` |
| `docs/TASK-REQUIREMENTS.md` | Tool and skill survey |
| `docs/X64-STATUS.md` | 21/23 subsystems build for AMD64 |
| `docs/KERNEL-BOOTS-XP.md` | The boot result and how to reproduce it |
| `docs/reference/P10-nasa-jpl.pdf` | NASA/JPL Power of Ten |
