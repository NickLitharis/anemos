# Kernel headers: what works and what does not (measured)

Attempted: include the tree's own `base/ntos/inc/ntos.h` into a freestanding
`i386-none-elf` build. Result: **11 residual errors**, all structural.

## What we solved on the way (all real, all reusable)

| Problem | Fix |
|---|---|
| `_INTEGRAL_MAX_BITS` undefined → `ntdef.h` maps `LONGLONG` to **double** | define `-D_INTEGRAL_MAX_BITS=64` |
| `__int64` unsupported | `-fms-extensions` |
| `MIDL_PASS` enables `[size_is(...)]` midl attributes that C cannot parse | do **not** define `MIDL_PASS` |
| `STDAPIVCALLTYPE` / `STDAPICALLTYPE` are MSVC-only spellings | map to `__stdcall` |
| `movzx eax, _PCR KPCR.Number` ambiguous operand size | `movzx eax, byte ptr _PCR ...` |
| `__try`/`__except` SEH in `ntrtl.h` inlines | not supported on this target; remove those inlines |
| Header case (`NtRtlStringAndBuffer.h`) | case map |
| Missing DDK roots, `prefetch.h` | add include dirs |

## What still blocks `ntos.h` (the honest boundary)

1. `C_ASSERT(sizeof(NPAGED_LOOKASIDE_LIST) == sizeof(PAGED_LOOKASIDE_LIST))`
   fails — the placeholder types we substitute do not have XP's real sizes.
2. `enum _DEVICE_USAGE_NOTIFICATION_TYPE` incomplete — defined in a header the
   freestanding include set does not reach.
3. `PSYSTEM_HANDLE_INFORMATION`, `PSYSTEM_POOL_INFORMATION` undeclared — these
   live in ntddk/ntifs territory.

Every one of these is a **type-graph** problem, not a code problem. `ntos.h`
describes the whole kernel object model; reproducing it outside razzle means
reproducing that model exactly.

## Consequence for `ke/`

`ke/yield.c` needs `KIRQL`, `PRKPRCB`, `PRKTHREAD`, `KiFindReadyThread`,
`KiSwapThread`, `KiLockDispatcherDatabase`. That is the dispatcher itself, not a
leaf primitive. **`ke/` is not reachable as a first target.**

## Where the reachable frontier actually is

- `base/ntos/rtl`: 9 files compile today (byteswap, checksum, guid, intbits,
  slistfunc, string, array, cnvint, random) — pure data structure and string
  logic, no kernel object model.
- The next reachable layer is the **RTL + its own data structures** (prefix
  tables, AVL, bitmaps) once the generic-table types are modelled.
- `ke`, `mm`, `io`, `ob`, `ps` all require the full kernel object model.

This is recorded so the boundary is not rediscovered: **adding kernel files is
not a matter of more shims; it is a matter of reconstructing the object model.**
