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

## Evidence kept locally

`~/anemos-private/docs/razzle-evidence/ntosrtl-build.log` — the full razzle log
`~/anemos-private/docs/razzle-evidence/ntosrtl.lib` — the artifact itself

The evidence is Microsoft-derived and is not part of this repository.

---

## Kernel layers built (verified with artifacts)

| Target | Artifact | Objects | Size | Key symbols |
|---|---|---:|---:|---|
| `base\ntos\rtl\up` | `ntosrtl.lib` | 68 | 4.6 MB | `RtlCompareString`, `RtlInitUnicodeStringEx` |
| `base\ntos\ke\up` | `ke.lib` | 78 | 1.3 MB | `KiSwapThread`, `KiFindReadyThread`, `NtYieldExecution` |
| `base\ntos\ex\up` | `ex.lib` | — | 4.6 MB | executive |
| `base\ntos\mm\up` | `mm.lib` | 64 | 2.2 MB | `MmAccessFault`, `MmWorkingSetManager` |

`mm.lib` contains the memory manager: `acceschk`, `addrsup`, `allocpag`,
`allocvm`, `buildmdl`, `checkpfn`, `mmfault` (`MmAccessFault@16`), `wsmanage`
(`MmWorkingSetManager`, `MmTrimAllSystemPagableMemory`).

## THE `-c` TRAP — read before building anything

`harness/razzle-env.cmd` sets `BUILD_DEFAULT=daytona -e -E -w -nmake -i` and
`razzle.sh` passes `-cZ`. **`-c` forces a clean**, which deletes `obj/` —
including *tracked prebuilt objects whose source is missing from the leak*.

Concretely: `base/ntos/ex/up/obj/i386/exinit.obj` (142,847 bytes, tracked)
provides `ExInitSystem`, `ExInitSystemPhase2`, `ExGetPreviousMode`,
`ExSystemExceptionFilter`, `ExInitializeSystemLookasideList`, `ExVerifySuite`.
`exinit.c` **is not in the leak**. `-c` deletes the object; the build then fails
with `NMAKE : U1073: don't know how to make 'obj\i386\exinit.obj'`.

**Build with `-Z` instead of `-cZ`:**

```sh
harness/razzle.sh 'base\ntos\ex\up' -Z     # correct
harness/razzle.sh 'base\ntos\ex\up' -cZ    # destroys exinit.obj
```

If an object was already destroyed: `git checkout -- Source/XPSP1/NT/base/ntos/ex/`
restores it. **Never `git clean` under `Source/`.**

---

## Full kernel library set (22 of 24 subsystems)

Built with `harness/razzle.sh '<dir>' -Z` after the rtl/ke/ex/mm first wave:

```
ntosrtl  ke  ex  mm  config(bconfig)  kd  ob  se  po  ps
arb / arbiter / halarbiter  io / iomgr / pnpmgr / CSQ
cache  lpc  dbgk  raw  fstub  FsRtl  vdm  verifier
wmi / wmiTrace  perf
```

Missing only:

| subsystem | blocker |
|---|---|
| `nls` | needs `bugcodes.h` generated from `bugcodes.mc` via `mc.exe` |
| `init` | depends on `nls` producing `MSG00001.bin` |

### The mc.exe problem is solved

`harness/bin/mc.cmd` substitutes the in-tree `base/mvdm/tools/mc.exe`, which
works under Wine:

```sh
wine cmd /c "W:\NT\base\mvdm\tools\mc.exe -h . -r . W:\NT\base\ntos\nls\bugcodes.mc"
  MC: Compiling W:\NT\base\ntos\nls\bugcodes.mc
  -> bugcodes.h (51,787 B), bugcodes.rc, MSG00001.bin (16,676 B)
```

Cross-check against the shipped `public/sdk/inc/bugcodes.h` (49,415 B): the
**set of 246 bug-code symbols is identical** — only formatting differs, because
this is a different mc version. Semantically correct.

### Tree hygiene

Building `arb`/`io` overwrites 5 tracked files (`public/ddk/lib/i386/*.lib`,
`public/halkit/lib/i386/halarbiter.lib`, `public/internal/base/inc/bugcodes.txt`).
Restore after any build:

```sh
git checkout -- Source/XPSP1/NT/public/
git status --porcelain -uall Source/ | grep -v '^??'   # must be empty
```

---

## COMPLETE: every kernel subsystem builds, every architecture variant

59 libraries across all 23 building subsystems. The multi-arch variants matter:
`init/mp` needs `ke/mp`, `init/pae` needs `ke/pae`. Build order per variant:

```sh
for v in mp pae paemp; do harness/razzle.sh "base\ntos\ke\$v" -Z; done
for v in mp pae paemp; do harness/razzle.sh "base\ntos\init\$v" -Z; done
```

| subsystem | libs | notes |
|---|---:|---|
| rtl | 1 | ntosrtl.lib |
| ke | 4 | up, mp, pae, paemp |
| ex | 1 | |
| mm | 1 | |
| config | 3 | config + bconfig |
| kd64 | 4 | up, mp, pae, paemp |
| ob, se, po, ps, cache, lpc, dbgk, raw, fstub, fsrtl, vdm, verifier, perf | 2 each | up + mp |
| arb | 4 | arbiter, halarbiter, up, mp |
| io | 7 | io, iomgr, pnpmgr, CSQ per variant |
| wmi | 3 | wmi + wmiTrace |
| **init** | **5** | up, mp, pae, paemp — all built |

`nls` is `TARGETTYPE=NOTARGET`: it exists only to generate `bugcodes.h` /
`MSG00001.bin`, which it does. That is a success, not a missing library.

### The mc fix

`harness/bin/mc.cmd` now filters the `-A`/`-v` flags that the build passes but
`base/mvdm/tools/mc.exe` does not accept under Wine.
