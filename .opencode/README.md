# opencode integration

What this project provides to opencode, and where it lives.

## Skills  (`~/.config/opencode/skills/`)

| Skill | Origin | Purpose |
|---|---|---|
| `anemos-os-dev` | written here | OS rules, toolchain, XP gotchas, oracle usage |
| `cpp-pro` | jeffallan/claude-skills (MIT) | modern C++20/23 |
| `embedded-systems` | jeffallan/claude-skills (MIT) | memory discipline + 5 reference files |

Confirmed loaded — opencode's log reports:

```
skills="[\"anemos-os-dev\",\"cpp-pro\",\"embedded-systems\",
         \"git-commit\",\"systematic-debugging\",\"write-tests\"]"
```

## Instructions

`AGENTS.md` at the repo root. opencode watches it (`watcher subscribe path=…/AGENTS.md`).
It carries the non-negotiables: `Source/` is oracle-only, XP wins disagreements, toolchain
is clang 23.1.3, `-Z` not `-cZ`, revert build side effects.

## Toolchain

Exported from `~/.profile`, so every shell opencode spawns has it:

```
clang/clang++/lld-link/llvm-ar/llvm-nm/clang-tidy/clang-format/clangd  23.1.3
ninja 1.13.2 · cmake 3.31.6 · ctest
```

Verified by running `env -i HOME=$HOME bash -lc 'command -v clang'`.

## Host verification tools

- `libFuzzer + ASan` via system clang-19 (the mingw tree carries no Linux runtimes)
- `scapy` — parses pcap without root, for P2 packet comparison
- `pexpect`, `vncdo`, `vmrun` — drive the XP oracle headlessly

## Not installed, deliberately

- `clang-tidy` from apt — the mingw tree already has 23.1.3, matching the compiler.
- `tcpdump` — needs root; `scapy` reads capture files instead.
- `xxd` — `hexdump` covers the same need.
- `operating-system-mentor` skill — 56 lines of philosophy, no rules or failure modes.
