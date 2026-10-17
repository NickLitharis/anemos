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
