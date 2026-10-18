# x64 status — measured, not assumed

This is the first honest measurement of how much of the XP SP1 kernel compiles for
**AMD64**, and where it stops. Everything below was produced on this machine by
running the tree's own razzle build with `AMD64=1`.

## Headline

**21 of 23 kernel subsystems build clean for x64 right now.**

The two failures are **missing source files**, not broken code. One source file
pair (`exinit.c`, `systime.c`) was never included in the leak, and `ex` is a
prerequisite for the kernel link — so the x64 kernel cannot link today.

That is a very different problem from the one the old plan described, which
assumed x64 was a multi-month porting research project.

## The toolchain exists in the tree

The decisive discovery. `Source/XPSP1/NT/tools/win64/x86/amd64/` holds a complete
AMD64 cross-compiler:

| Binary | Role |
|---|---|
| `cl.exe` | x86-hosted AMD64 compiler |
| `c1.dll`, `c2.dll` | compiler passes |
| `link.exe`, `lib.exe` | AMD64 linker and librarian |
| `ml64.exe` | AMD64 assembler |

These are **PE32 (i386-hosted)** — so plain Wine32 runs them. Verified:

```
$ wine .../win64/x86/amd64/cl.exe
Microsoft (R) C/C++ Optimizing Compiler Version 13.00.9269.1 for AMD64
```

and it produces genuine x86-64 output:

```
$ file t.obj   ->  x86-64 COFF object file
$ file t64.exe ->  PE32+ executable for MS Windows 5.01 (native), x86-64
```

Razzle itself already supports the target. `tools/makefile.plt`:

```
!elseif $(AMD64)
TARGET_DEFINES=-D_AMD64_ -D_WIN64
TARGET_DIRECTORY=amd64
```

So an x64 kernel build is `AMD64=1`, not a port.

## How to build

```bash
harness/razzle-amd64.sh 'base\ntos\nls'       -Z   # first: emits bugcodes.h
harness/razzle-amd64.sh 'base\ntos\rtl\up'    -Z
harness/razzle-amd64.sh 'base\ntos\ke'        -Z   # builds up + mp
harness/razzle-amd64.sh 'base\ntos\mm'        -Z
harness/razzle-amd64.sh 'base\ntos\kd64'      -Z
harness/razzle-amd64.sh 'base\boot\bootvid'   -Z
harness/razzle-amd64.sh 'base\boot\kdcom'     -Z
harness/razzle-amd64.sh 'base\ntos\init\up'   -Z   # needs all of the above, and ex
```

Output goes to `Source/XPSP1/binaries.amd64fre/`.

`harness/razzle-amd64.sh` and `harness/razzle-amd64-env.cmd` are the x64
counterparts of the x86 harness: same shape, `AMD64=1`, and a `PATH` that puts
the AMD64 cross-compiler first.

## Status per subsystem

| Subsystem | x64 | Files |
|---|---|---|
| rtl | **OK** | 175 |
| ke | **OK** | 144 |
| mm | **OK** | 128 |
| ob | **OK** | 38 |
| se | **OK** | 58 |
| ps | **OK** | — |
| po | **OK** | 52 |
| io | **OK** | 160 |
| cache | **OK** | 30 |
| config | **OK** | — |
| lpc | **OK** | 30 |
| dbgk | **OK** | 8 |
| raw | **OK** | 22 |
| fstub | **OK** | 10 |
| fsrtl | **OK** | 36 |
| vdm | **OK** | 4 |
| verifier | **OK** | 48 |
| wmi | **OK** | 44 |
| perf | **OK** | 12 |
| arb | **OK** | 12 |
| nls, kd64, bootvid, kdcom | **OK** | — |
| **ex** | **BLOCKED** | needs `exinit.c`, `systime.c` |
| **init** | **BLOCKED** | needs `ex` |

The RTL artifact is a real 4.7 MB `ntosrtl.lib` whose members are COFF machine
type `0x8664`. Not mislabeled.

## Three defects fixed to get here

All three are real holes in the leak, not workarounds.

**1. `kxamd64.inc` has no `STATUS_*` equates.** The x64 `.asm` sources use 23
`STATUS_*` constants. The i386 counterpart `ks386.inc` defines 40 of them;
`kxamd64.inc` defines none (`grep -c equ` = 4, all comments).
Fix: `tools/gen_amd64_status_inc.py` derives the table from
`public/sdk/inc/ntstatus.h` and appends it to `ksamd64.inc` behind an
`IFNDEF` guard. All 22 values that also appear in `ks386.inc` match numerically.

**2. MASM rejects `C0000005H`.** A hex literal beginning with a letter is parsed
as a *symbol*. `ml64` reports this as the misleading `A2006: cord redefinition`.
A leading zero (`0C0000005H`) forces numeric parsing — which is why `ks386.inc`
values look 9 digits wide. The generator applies this rule.

**3. `MmPagedPoolStart` was declared `const` only for amd64.**
`base/ntos/mm/amd64/dataamd.c` said `const PVOID`, while `mi.h` declares
`extern PVOID` and `mminit.c:798` **assigns** to it. i386 and ia64 both use plain
`PVOID`. Removed the `const`.

Also added: `PsMxCsr equ 00074H` to `ksamd64.inc` — a `KPROCESSOR_STATE` offset
used by `ke/amd64/procstat.asm`. The value was computed by hand and then
**confirmed independently** by compiling an `offsetof()` probe with the tree's own
AMD64 `cl.exe`, which reported `0x74` and `sizeof` `0x78`.

## What exactly blocks the link

`base/ntos/ex/sources.inc` lists `..\exinit.c` and `..\systime.c`. Neither file
exists anywhere in the tree, and `git ls-files` confirms they were never included.
For i386 and ia64 this is masked by prebuilt objects
(`base/ntos/ex/{up,mp}/obj/i386/exinit.obj`) — **but no `obj/amd64/` equivalents
exist**, so x64 has nothing to fall back on.

These same two files are the only such gap in the whole kernel: a scan for
objects present for i386 and ia64 but absent for amd64 returns exactly
`exinit.obj` and `systime.obj`.

Scope of the reconstruction, from the i386 objects' global symbols:

- `exinit.obj` — 18 functions: `ExInitSystem`, `ExInitSystemPhase2`,
  `ExpInitSystemPhase0/1`, `ExInitPoolLookasidePointers`,
  `ExInitializeSystemLookasideList`, `ExComputeTickCountMultiplier`,
  `ExGetPreviousMode`, `ExVerifySuite`, `ExSystemExceptionFilter`, `InitializeListHead`,
  `InitializeSListHead`, …
- `systime.obj` — 30 functions: `ExpSetSystemTime`, `ExInitializeTimeRefresh`,
  `ExLocalTimeToSystemTime`, `ExAcquireTimeRefreshLock`, the time-refresh and
  timezone DPC/work routines, …

So: **48 named functions, with their signatures recoverable from the prebuilt
i386 objects and the exported prototypes in `ntoskrnl.src`.** Bounded work, not
open-ended — but it is real work, and it is the current gate on an x64 kernel.

`ExpInitSystemPhase0/1` and `ExpSetSystemTime` are not small functions. This is
where the honest estimate stops being "a weekend."

## What is still not started

- **HAL.** `base/hals/halacpi/amd64/` has 30 files and
  `base/hals/x86new/amd64/` has a `sources` declaring 24 — far more than the old
  plan's claim of "no halx64". Neither has been built yet.
- **Linking an x64 kernel image.** Blocked on `ex`.
- **Booting x64.** Requires a completely different boot path (`ntldr` → x64
  `osloader`, x64 HAL, x64 drivers). Nothing attempted.
- **win32k / shell x64.** Out of scope, as before.

## Reproducing the numbers

Every claim above came from a command in this document. The two that matter most:

```bash
# does the x64 toolchain work at all?
wine "$NT/tools/win64/x86/amd64/cl.exe"    # must print "... for AMD64"

# how much builds?
for d in rtl ke mm ob se ps po io cache config lpc dbgk raw fstub fsrtl \
         vdm verifier wmi perf arb; do harness/razzle-amd64.sh "base\\ntos\\$d" -Z; done
```
