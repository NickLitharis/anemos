# The tree's own razzle build works under Wine

**Status: VERIFIED with artifacts.** Not a report — a 4.8 MB library built from
Microsoft's own compiler.

## The discovery

The leaked tree ships a complete Microsoft toolchain inside itself:

```
Source/XPSP1/NT/tools/x86/cl.exe       MSVC compiler driver     84 KB
Source/XPSP1/NT/tools/x86/c1.dll       C front end             888 KB
Source/XPSP1/NT/tools/x86/c1xx.dll     C++ front end           2.0 MB
Source/XPSP1/NT/tools/x86/link.exe     linker                  612 KB
Source/XPSP1/NT/tools/x86/nmake.exe    make
Source/XPSP1/NT/tools/x86/rc.exe       resource compiler
Source/XPSP1/NT/tools/x86/masm386.exe  assembler (ml)
Source/XPSP1/NT/tools/x86/build.exe    razzle build driver
Source/XPSP1/NT/tools/makefile.plt     razzle makefile include
```

Nobody had to reconstruct the build system. It was already there.

## The proof

```sh
harness/razzle.sh 'base\ntos\rtl\up'
```

Output artifact:

```
Source/XPSP1/NT/base/ntos/rtl/up/obj/i386/ntosrtl.lib
  4,798,382 bytes · current ar archive · 68 members · untracked (newly built)
```

Members are genuine Microsoft COFF objects:

```
obj_i386_string.obj : Intel i386 COFF object file, not stripped, 41 sections
  00000000 T _RtlAppendAsciizToString@8
  00000000 T _RtlCompareString@12
  00000000 T _RtlEqualString@12
  00000000 T _RtlInitUnicodeStringEx@8
  00000000 T _RtlPrefixString@12
  00000000 T _RtlUpperChar@4
```

The `@N` stdcall decoration is MSVC codegen, not a shim.

## Why this changes the project

| Blocker | Before | Now |
|---|---|---|
| Build system (razzle/MSVC6/DDK) | "unobtainable" | **works under Wine** |
| `ntos.h` `C_ASSERT` failures | blocked `ke/`, `mm/`, `io/` | **gone** — the real headers compile with the real compiler |
| Kernel RTL | 213 routines via hand-written shims | 68 objects from Microsoft's compiler |
| Struct layouts | guessed, then verified by hand | **whatever Microsoft's compiler produced** |

## How it works

1. `harness/razzle.sh <tree-relative-dir>` sets up a razzle environment in
   `harness/razzle-env.cmd` and runs the tree's `build.exe` under Wine
   (`WINEPREFIX=~/.wine-xp`, with `w:` mapped to `Source/XPSP1`).
2. Razzle reads the directory's `sources` file, invokes `ml.exe` for `.asm` and
   `cl.exe` for `.c`, then `link.exe`.
3. Output lands in `<dir>/obj/i386/` and `Source/XPSP1/binaries.x86fre/`.

## Caveats

- `mc.exe` (message compiler) cannot take `-h`/`-r` under Wine; `harness/bin/mc.cmd`
  substitutes the in-tree `base/mvdm/tools/mc.exe`. Its message-table binary
  output differs from the official one, so event-log message text is unverified.
- Never run `rm`/`git clean` under `Source/`: it contains 277,866 tracked files
  **including prebuilt binaries inside `obj/` directories** that shipped with
  the repo. Deleting them destroys tracked content.
- Build logs (`build.log`, `build.err`) and `obj/` output appear inside the tree.
  Remove them by exact path only.

## Evidence kept in-repo

`docs/razzle-evidence/ntosrtl-build.log` — the full razzle log
`docs/razzle-evidence/ntosrtl.lib` — the artifact itself
