# Anemos — agent instructions

Anemos is an open-source operating system inspired by Windows XP: new code, 2026
standards, validated for behavioural compatibility against XP itself.

**Read `PLAN.md` first.** It supersedes `MODERNIZATION_PLAN.md`,
`docs/OPEN-REPLACEMENT-PLAN.md` and `docs/ANEMOS-PLAN.md`, which are kept for history.

## Non-negotiables

1. **`Source/` is leaked Microsoft code.** It is a specification and test oracle only.
   Never transcribe from it. Never modify it to make something work. "Modernising" it —
   putting C23 syntax over Microsoft's logic — is a derivative work and is banned.
   See `NOTICE.md` and `SCOPE.md`.
2. **XP is the oracle.** Where our behaviour differs from XP's, XP is right. Record the
   conflict; do not "fix" XP.
3. **Toolchain is pinned:** clang 23.1.3 + lld-link 23.1.3 (`tools/toolchain.lock`).
   Anemos code is never built by the 2001-era MSVC in the tree; that compiler exists only
   to build the legacy XP kernel used as the oracle.

## Before writing code

- `docs/OS-CODING-RULES.md` — 17 rules, each naming the failure mode it prevents.
- `docs/SIZE-POLICY.md` — `-Oz`, ≤64 KB `.text` per subsystem. Measured levers, and two
  flags that turned out to be folklore.
- `docs/TASK-REQUIREMENTS.md` — what tooling exists and what is still missing.

## Gates

Nothing in P1-P4 starts until these pass:

- **G1 ABI proof** — do clang 23 and MSVC 13.00 agree on struct layout and calling
  convention? If not, every replacement silently corrupts structures.
- **G2 conformance runner** — must report *identical* when pointed at `xp.vmdk` and
  `xp-pristine.vmdk`. If it reports a difference, the runner is wrong, not the kernels.

## Working rules

- **Build legacy code with `-Z`, never `-cZ`.** `-c` wipes `obj/` and destroys tracked
  prebuilt objects whose source is missing from the leak — sometimes the only copy.
- **Revert build side effects.** The amd64 build rewrites tracked files under `public/`
  (CRLF `bugcodes.txt`, rebuilt `.lib`s). `git checkout --` them; they are not changes.
- **A check that cannot fail is not a check.** A test run reporting zero tests failed.
- **Do not claim a flag works without verifying it in the output.** `/guard:cf` sets a PE
  bit and enforces nothing without `_load_config_used`. It is omitted.
- `puthex` prints **hex**. 0x10 is 16, not 10. This has caused false failures twice.

## The oracle

`xp-pristine.vmdk` = Microsoft's kernel (control). `xp.vmdk` = ours (subject). Same disk.
Swap binaries host-side with `tools/ntfspatch.py` (no root, VM must be off). Drive the
guest with `vmrun` + `vncdo` on `127.0.0.1:5901` (password `xpvm`). VMware VNC cannot type
`:` — do not build a workflow that needs it.

## Layout

```
PLAN.md              authoritative plan
SCOPE.md/NOTICE.md   legal boundary
docs/                rules, policies, measurements
harness/             razzle wrappers (x86 and amd64)
tools/               ntfspatch.py, codegen, toolchain.lock
kernel/ boot/        earlier freestanding experiments
Source/              Microsoft's leaked code — reference only
```
