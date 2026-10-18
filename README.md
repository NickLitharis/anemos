# Anemos

An open-source operating system inspired by Windows XP, written to 2026 standards.

Not a fork. Not a leaked release. New code, written against public specifications,
validated against XP's observed behaviour.

## Status: research, not a working OS

This repository is currently a build-and-verification workbench, not an operating
system. What actually works today:

| Capability | State |
|---|---|
| Build the XP kernel subtree from source | ✅ verified |
| Boot that kernel into a full XP install | ✅ verified (`docs/KERNEL-BOOTS-XP.md`) |
| Build 21 of 23 kernel subsystems for **AMD64** | ✅ verified (`docs/X64-STATUS.md`) |
| Conformance harness comparing our build to XP | ❌ not built yet |
| Any replacement subsystem | ❌ not started |

There is no Anemos kernel yet. There is a proven method for producing and verifying
one, and a measured map of what stands in the way.

## Read this first

- **`PLAN.md`** — the authoritative plan. Everything else is reference.
- `SCOPE.md` — exactly which files are ours, and which are not
- `NOTICE.md` — the clean-room policy, and why it is strict
- `docs/OS-CODING-RULES.md` — 17 rules, each naming the failure it prevents
- `docs/TOOLCHAIN-POLICY.md` — clang/lld 23.1.3, pinned
- `docs/SIZE-POLICY.md` — measured size levers and budgets
- `docs/X64-STATUS.md` — measured x64 build status: 21/23 subsystems

## The rules this project follows

Because this is OS code, mistakes are not recoverable at runtime. The engineering
rules are in `docs/OS-CODING-RULES.md`. They are not style preferences.

## About `Source/`

`Source/` contains leaked Windows XP source code belonging to Microsoft. It lives
in this working copy as a **reference and test oracle** and nothing else.

It is not owned by this project, not licensed by it, not distributed by it, and
contributed code must never be derived from it. **If you fork this, exclude
`Source/`.** See `NOTICE.md`.

## License

MIT for our own code — exactly which files, in `SCOPE.md`.
Nothing in `Source/` is licensed to you.
