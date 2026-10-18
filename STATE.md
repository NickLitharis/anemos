# Project state (updated as work proceeds)

## Current status: our source-built `ntoskrnl.exe` boots a full Windows XP install

All 59 kernel libraries build with the tree's own MSVC toolchain under Wine, and the
resulting kernel boots a complete XP SP1 installation to a working Luna desktop. It has
1,464 exports — the same name set as Microsoft's, with an identical `.edata` size. Verified
by hashing the kernel back out of the VMDK. **See v2.0 at the end of this file** and
`docs/KERNEL-BOOTS-XP.md`.

The sections below are the earlier freestanding work: reimplementing XP's internals against
hand-written shims. Still useful, no longer the main road.

---

## Verified (freestanding): XP SP1 kernel source executing on modern hardware

Build: `./kernel/build.sh ./kernel/out` -> `libxprtl.a` (61 KB, 140 routines,
14 XP source files). Runtime: `./boot/build.sh` + QEMU.

| XP source file | Verified by |
|---|---|
| `rtl/byteswap.c` | exact values for 3 byte-swap routines |
| `rtl/intbits.c` | interlocked bit ops return previous value |
| `rtl/checksum.c` | CRC-32 of "123456789" = 0xCBF43926 |
| `rtl/slistfunc.c` | SLIST init/depth round-trip |
| `rtl/guid.c` | GUID parsed via XP's own ScanHexFormat |
| `rtl/string.c` | case-sensitive vs insensitive compare differ correctly |
| `rtl/cnvint.c` | decimal + hex conversion, round-trip parse |
| `rtl/bitmap.c` | set/clear/test/count on a 128-bit bitmap |
| `rtl/avltable.c` | 7 inserts -> depth 3 (rotations ran), lookup hit+miss |

**9 files verified.** Additionally `array.c`, `prefix.c`, `random.c`, `splay.c`
compile and link; `prefix.c` is **not** verified (second-insert hang, narrowed
in `kernel/NOTES-prefix.md`).

## Environment (all in-repo, no external SDKs)

- `kernel/inc/xpntdef.h` — NT types derived from the tree's own `ntdef.h`
- `kernel/inc/xpkernel.h` — calling conventions, STATUS codes, NLS tables,
  memory primitives, 64-bit division helpers, splay macros, **DbgPrint**
- `boot/` — multiboot image builder; tests assert values in-guest

## Hard boundary (measured, not assumed) — SUPERSEDED, see v2.0 below

> **No longer the boundary.** This section describes what happens when you try to
> compile `base/ntos` *freestanding*, with hand-written shims. That still fails as
> described. But the kernel does not have to be compiled that way: the tree ships
> its own MSVC toolchain, and building with razzle under Wine compiles and links the
> whole kernel, which then boots a real XP install. Read this as "the freestanding
> route hits a wall", not "the kernel is unreachable".

Including the tree's own `base/ntos/inc/ntos.h` freestanding fails with 11
residual errors, all type-graph problems (`C_ASSERT` size asserts,
`SYSTEM_*` types, incomplete enums). `ke/`, `mm/`, `io/`, `ob/`, `ps/` need the
kernel object model and are **not reachable by adding shims**.
See `kernel/README2.md`.

## Harness (user-mode builds)

`harness/xpbuild.py` builds real XP user-mode targets (eventcreate 338 KB,
bootini 493 KB, umxdebug.dll, smxdebug, setup, cmdline.lib, sr/api, pstodib).
21 error classes auto-repaired. Broad random sample across the whole tree is
still ~0/29 — that is the DDK-port problem, not a harness defect.

---

## v1.0 — the STATUS→DOS error table (835 pairs)

`base/ntos/rtl/generr.c` turned out to be **data, not runtime code**: it holds
`LONG CodePairs[]`, the authoritative status/DOS pair list that the razzle build
feeds to `generr.exe` to emit `RtlpRunTable`. That generator cannot be run here,
so `kernel/gen_runtable.py` emits the runtime table from the same data, resolving
each name through the tree's own `ntstatus.h` / `winerror.h`.

- 835/838 pairs resolved (3 are the file's marker strings).
- `kernel/src/runtable_gen.c` is generated and checked in; regenerate with
  `python3 kernel/gen_runtable.py`.

Spot-checked in-guest against documented Windows behaviour:

```
pairs=343 (0x343 = 835) OK
ACCESS_DENIED=5 OK        NO_MEMORY=8 OK        NOT_FOUND=2 OK
INVALID_HANDLE=6 OK       BUFFER_OVERFLOW=EA OK (ERROR_MORE_DATA, per generr.c)
```

`libxprtl.a` now holds **146 routines / 15 XP sources**.

### Repeated lesson

`puthex` writes hexadecimal. `0x343` is 835. I twice read a correct result as a
failure because of this — the in-guest assertions were right both times. Read
the base before drawing a conclusion.

---

## v1.1 — XP handle tables verified

`base/ntos/rtl/handle.c` (9 routines) builds and verifies: allocate, free,
validate by handle and by index, over XP's chunked handle tables.

This needed three things that also unblock the wider RTL:

- `RTL_CRITICAL_SECTION` / `_DEBUG` with the tree's real layout
- `RTL_HANDLE_TABLE` / `_ENTRY` copied verbatim from `public/sdk/inc/nturtl.h`
- `MEM_*` and `PAGE_*` constants harvested from the tree's `winnt.h` (12 + 16)
- a freestanding `NtAllocateVirtualMemory`/`NtFreeVirtualMemory` shim over a
  1 MB static arena (`kernel/src/arena.c`), so XP's allocation logic runs
  unchanged

### A test assumption corrected by reading XP's own comment

`RtlAllocateHandle` deliberately leaves the ALLOCATED flag **clear**:

> "Clear free list link field, which also leaves the handle allocated bit
> clear. This allows the caller to mark it is allocated after they are done
> filling in their portion."   — `handle.c`

My test asserted the flag was set after allocation and reported FAIL. XP was
right; the test was wrong. Corrected to set the bit as the caller would.

`libxprtl.a`: 155 routines, 18 XP sources. 11 verified suites, all passing.

---

# v2.0 — the real kernel builds AND boots a full XP install

Everything above this line was freestanding experiments: reimplementing XP's
internals against hand-written shims, one routine at a time. That work is real
and still useful for understanding, but it is **no longer the main road**.

The tree ships its own MSVC toolchain in `NT/tools/x86/` (`cl.exe`, `c1.dll`,
`c1xx.dll`, `link.exe`, `nmake.exe`, `ml.exe`, `rc.exe`, `build.exe`). It runs
under Wine. That removed the entire "can this even compile" question.

## What is true now (each item verified, not inferred)

| Claim | How it was checked |
|---|---|
| All 59 kernel libraries build | every `base/ntos/*` variant: up, mp, pae, paemp |
| `ntoskrnl.exe` links | 1,918,976 bytes, **1,464 exports — same name set as Microsoft's** |
| Export surface really matches | `.edata` size identical to the shipped kernel: `0xb258` |
| Debuggable | real 6.1 MB `ntoskrnl.pdb` produced |
| Supporting binaries | `hal.dll` 101,376 · `bootvid.dll` 12,288 · `kdcom.dll` 7,168 |
| **It boots** | full XP SP1 install → Luna desktop, responsive `cmd.exe` |
| The booting kernel is *ours* | hashed back out of the VMDK: md5 `96896c94…`, Microsoft's `b9080d97…` absent |

Full procedure and evidence: `docs/KERNEL-BOOTS-XP.md`, `docs/KERNEL-BUILDS.md`.

## How the swap is done (the only reliable way)

Windows locks `ntoskrnl.exe` while running. Recovery Console works in principle
but cannot be driven over VMware's VNC server, which drops `:` — this cost hours.
The answer is to stop using the guest keyboard: `tools/ntfspatch.py` patches the
VMDK from the host, parsing the VMDK grain directory and NTFS's MFT and indexes.
No root, no loop device, no nbd, no VMware Tools.

## Two mistakes that cost a working install

1. **Do not resize the file in its MFT record.** Our kernel is 123,264 bytes
   smaller than Microsoft's. Editing `$DATA`'s real-size field corrupted the
   record: `dir` reported `0 ntoskrnl.exe` and the file became unopenable —
   while the already-loaded kernel kept running, which made it look fine.
   Keep the original size and zero-pad instead; Windows honours the PE headers.
2. **Do not re-apply the update-sequence array when rewriting an MFT record.**
   Write file data through the runlist and leave records alone.

Backup first (`cp xp.vmdk xp-before-kernelswap.vmdk`); restoring it recovers.

## Rule for the build

Always `-Z`, never `-cZ`. `-c` wipes `obj/`, destroying tracked prebuilt `.obj`
files whose source is missing from the leak — sometimes the only copy.

## What this does and does not prove

It proves the kernel **initialises, runs a complete install, and drives the GUI**.
It does not prove every subsystem is correct — nothing driver-heavy has been
exercised yet. Next meaningful test: disk I/O stress, networking, and a 3D game,
diffed against `xp-pristine.vmdk` running Microsoft's kernel on the same disk.

## Where the plan stands

`MODERNIZATION_PLAN.md` has been re-based on this: **M0 is done**, and
**M2 (performance) is now the live track** rather than a blocked aspiration.
The whole VM is drivable headlessly, so any perf change can be A/B'd by swapping
one file in the VMDK and rebooting.
