# kernel/ — freestanding build of XP kernel sources

Turns XP kernel `.c` files into relocatable objects with no razzle, no MSVC and
no DDK installation, then links them into bootable images.

```
$ ./kernel/build.sh ./kernel/out
  built array_standalone.o
  built byteswap_standalone.o
  ...
libxprtl.a: 19932 bytes

$ ./boot/build.sh ./boot/out
built ./boot/out/xp-v05.elf
```

## Layout

| Path | Purpose |
|---|---|
| `inc/xpntdef.h` | NT types **derived from the tree's own `public/sdk/inc/ntdef.h`** — layouts and constants are XP's, not approximations |
| `inc/xpkernel.h` | kernel environment: calling conventions, annotations, `STATUS_*`, `va_list`, interlocked ops, NLS hooks, memory primitives |
| `src/*.c` | XP sources with `#include` lines replaced by `#include "xpkernel.h"` (bodies untouched) |
| `rtl_*.c` | older per-file standalones kept for reference |
| `build.sh` | compiles every source to `out/*.o` and archives `libxprtl.a` |

## Sources currently building (7 files, 44 routines)

`byteswap.c` · `checksum.c` · `guid.c` · `intbits.c` · `slistfunc.c` ·
`string.c` · `array.c`

## Verified at runtime (boot/images, asserted in-guest)

| Routine family | Assertion |
|---|---|
| byte swaps | exact values |
| interlocked bit ops | return previous value + resulting flags |
| `RtlComputeCrc32` | 0xCBF43926 for "123456789" |
| SLIST depth | round-trip |
| `RtlGUIDFromString` | parses a GUID via XP's `ScanHexFormat` |
| `RtlInitUnicodeStringEx`, `RtlCompareString`, `RtlEqualString`, `RtlUpperChar` | lengths, case-(in)sensitive equality, upcasing |

## Environment facts learned (do not rediscover)

- Compile with `-fms-extensions`; `__int64` and MS asm blocks need it.
- `_INTEGRAL_MAX_BITS=64` must be defined or `ntdef.h` maps `LONGLONG` to
  `double`.
- `i386-none-elf` has no `windows.h` and no CRT: `wcslen`/`strlen`/memory
  primitives must be supplied (they are, inline, in `xpkernel.h`).
- Wide literals are `int`-sized on this target; build `WCHAR` buffers by code
  point, never by assigning `L"..."`.
- Kernel sources assume NLS tables (`NlsAnsiToUnicodeData`,
  `NlsMbCodePageTag`, `NLS_UPCASE`). With `NlsMbCodePageTag = 0` the ASCII fast
  path is taken and the tables are never dereferenced.
