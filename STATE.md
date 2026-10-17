# Project state (updated as work proceeds)

## Verified: XP SP1 kernel source executing on modern hardware

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

## Hard boundary (measured, not assumed)

Including the tree's own `base/ntos/inc/ntos.h` freestanding fails with 11
residual errors, all type-graph problems (`C_ASSERT` size asserts,
`SYSTEM_*` types, incomplete enums). `ke/`, `mm/`, `io/`, `ob/`, `ps/` need the
kernel object model and are **not reachable** by adding shims.
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
