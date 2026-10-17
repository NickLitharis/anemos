# M0 — Compile probe: PASSED at the object stage

**Target:** `Source/XPSP1/NT/admin/cmdline/eventcreate` — 1,190 LOC, single translation unit,
Win32 only (no kernel/win32k headers). Probe chosen because it is the smallest *complete*
program in the tree.

## Result

The original Microsoft source compiles to a 15,422-byte object file with zero errors.
No source edits were required. The tree needs a dialect-compatible compiler, not a rewrite.

## Toolchain

- `llvm-mingw 20261006` (msvcrt), unpacked to `~/.local/opt/llvm-mingw-20261006-msvcrt-ubuntu-22.04-x86_64/`
- `i686-w64-mingw32-clang -fms-extensions`
- Headers: `Source/XPSP1/NT/public/sdk/inc` + `Source/XPSP1/NT/public/internal/admin/inc`
- Defines: `-DUNICODE -D_UNICODE -D_M_IX86=500`

```sh
NT=/home/marocos/winxp_source/Source/XPSP1/NT
i686-w64-mingw32-clang -fms-extensions -c eventcreate.c -o ev3.o \
  -I$NT/public/sdk/inc -I<casemap> -I$NT/public/internal/admin/inc -I<gen> \
  -DUNICODE -D_UNICODE -D_M_IX86=500
# -> ev3.o, 15422 bytes, 0 errors
```

`<casemap>` is a directory containing `EventCreate.h` (the file is stored as `eventcreate.h`),
and `<gen>` contains the message header generated from `evcrtmsg.mc`.

## Error ledger

| # | Class | Sites | Verdict |
|---|---|---|---|
| 1 | Case sensitivity: `sources`/`#include` use `EventCreate.cpp`/`.h`, tree stores lowercase | 2 files | mechanical; case-map dir or rename |
| 2 | `evcrtmsg.mc` is never compiled by a standalone build → `MSG_EVENTID_START/END` undefined | 1 generator | generate header from `MessageId=`/`SymbolicName=` pairs |
| 3 | MSVC inline-asm intrinsics in `winnt.h` (`Int64ShllMod32`, `Int64ShraMod32`, `Int64ShrlMod32`, `GetFiberData`, `GetCurrentFiber`) | 5 funcs | **not a blocker**: `clang -fms-extensions` compiles the `__asm {}` blocks natively |
| 4 | `DATE` (`typedef double DATE` in `wtypes.h`) collides with a member name in `oaidl.h` | 1 | GCC-only; clang unaffected |

Only classes 1 and 2 need work. Classes 3 and 4 disappear on the correct toolchain.

## What remains before M0 closes

Linking. `eventcreate.c` references 18 symbols supplied by `cmdline.lib`
(`_DoParseParam`, `_ShowMessage`, `_GetResString`, `_CreateDynamicArray`, `_CloseConnection`, …).
That library is **in-tree source** under `public/internal/admin`, not a prebuilt binary.
Next step: build `cmdline.lib` from source, link, run under Wine.

## Evidence

- `EvcrtMsg.h.generated` — the message header produced from `evcrtmsg.mc`.
- Object file and reproducer live outside the repo (`/tmp/m0`) to keep the tree clean.
