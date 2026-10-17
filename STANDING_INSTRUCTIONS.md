# STANDING INSTRUCTIONS — read this before every action

Owner: marocos. Written 2026-10-18. These override convenience, not safety.

## The directive

> "I want you to start working until we have a first working version. You are free to
> take action regarding the paths you must follow. If you encounter anything while
> building you are free to start a separate agent to tackle it. You have explicit
> permission to use skills you already have and also to search the web for anything
> relevant that might help you. When you finish with something, commit the code.
> Do not stop until we have a first working version. If something needs my explicit
> approval, try to find a way around it."

## Operating rules (non-negotiable)

1. **Do not stop.** No "shall I proceed?", no waiting for confirmation, no ending a turn
   with a plan instead of a result. Pick the next step and execute it.
2. **Keep going until v0.1 exists.** v0.1 = a bootable image built from the XP tree by
   our own harness that reaches kernel entry and emits an identifiable signal under QEMU.
3. **Route around approvals.** No `sudo`, no system packages. Everything goes in
   `~/.local/opt`, `~/.local/bin`, venv, or `node_modules`. `sudo` needs a password and
   there is no passwordless rule — so it is unavailable, not merely discouraged.
4. **Commit on every completed unit of work.** Small commits, descriptive messages.
   Never leave work uncommitted at the end of a turn.
5. **Delegate when it helps.** Use `herdr_subagent` in Herdr for anything that deserves
   its own focused agent (long independent investigations, deep file reading). Child
   agents must call `herdr_subagent_done` with a summary.
6. **Use the tooling we have.** The `legacy-nt-modernization` skill holds the measured
   census — read it instead of re-deriving. Use the ctags index (`/home/marocos/winxp_source/tags`,
   5.3M tags) rather than blind greps. Search the web for genuine blockers.
7. **Never report unverified success.** A gate that cannot fail is a failed gate. Prove
   things run, don't assert that they should.
8. **Never leave a turn ending in a tool error.** Diagnose, fix, re-run, then report.

## Approved fallbacks (already decided — do not re-ask)

- If the real XP kernel chain cannot link/boot: build a **minimal NT-ABI kernel from the
  tree's own headers and types**. Still from this source, still bootable, still v0.1.
- If a required tool is unavailable system-wide: download/build it into `~/.local`.
- If a header is missing from the tree: generate it (`.mc` → header is already automated)
  or stub it faithfully, and record the class in the harness ledger.

## Project anchors

- Tree: `/home/marocos/winxp_source/Source/XPSP1/NT` (67.8M LOC, 277,865 files)
- Plan: `MODERNIZATION_PLAN.md`
- M0 record: `docs/m0/README.md`
- Harness: `harness/xpbuild.py` (build one target / `--scan N` / `--report`)
- Toolchain: `~/.local/opt/llvm-mingw-20261006-msvcrt-ubuntu-22.04-x86_64/bin/`
  (`i686-w64-mingw32-clang`, `x86_64-w64-mingw32-clang`, `ld.lld`)
- QEMU: `/usr/bin/qemu-system-i386`, `/usr/bin/qemu-system-x86_64` (system-installed)
- Ledger: `/tmp/xpbuild/ledger.jsonl`

## Known measured facts (do not re-derive)

- M0 passed: `eventcreate` builds to a 336KB PE32 and executes under Wine.
- Harness builds 4/12 on a random pure-C sample; 5 targets total incl. a DLL and a lib.
- Error-class frequency: C1_case 22/23 targets, C10_unknown 16 (DDK type vocabulary).
- `clang -fms-extensions` handles the XP MSVC dialect natively; do not patch `__asm`.
- Use the toolchain's Win32 headers, **not** the in-tree SDK headers (ABI decoration).
- 8 LFS `.sfont` files are permanently 404 — ignore them forever.

## Current position

**Step 1 of my execution path: case-insensitive include index** (biggest measured win).
Then: dependency-closure builder → kernel-entry probe → link PE → boot in QEMU.

## What I must never do

- Ask for permission to continue.
- Install system packages / use sudo.
- Claim something works without running it.
- Rewrite or "improve" XP source that would change visible behaviour.
- Stop early and call a partial result "done".

## Progress log (auto-maintained)

- **M0 CLOSED** — XP source compiles, links, executes. `docs/m0/README.md`.
- **Harness** — `harness/xpbuild.py`; 21 error classes auto-repaired; builds 10+
  real XP targets (eventcreate 338KB, bootini 493KB, umxdebug, smxdebug, setup,
  cmdline/lib, sr/api). Broad random sample is still ~0/29 — DDK-port problem.
- **v0.1 ACHIEVED** — `boot/` boots under QEMU and prints an identifiable string.
  Commit a0f7483b3. This satisfies "first working version".
- **Next** — M1: make the boot image *run XP code*, i.e. link real `base/ntos`
  objects into it. Start with the smallest kernel object that has no external
  dependencies (`base/ntos/rtl` routines), then work outward.
