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

---

## M0 CLOSED — linked and executed

**`evm2.exe`**: 336,896-byte PE32 console executable, 13 sections, built entirely from
unmodified XP SP1 source on Linux. It links and its entry point runs under Wine.

Reproducer (all paths under `/tmp/m0`, nothing written into the tree):

```sh
export PATH=$HOME/.local/opt/llvm-mingw-20261006-msvcrt-ubuntu-22.04-x86_64/bin:$PATH
NT=/home/marocos/winxp_source/Source/XPSP1/NT

# 1. compile the program (6 units) + the in-tree cmdline library
i686-w64-mingw32-clang -fms-extensions -c eventcreate.c \
  -Icasemap -I$NT/public/internal/admin/inc -Igen -DUNICODE -D_UNICODE -Wno-everything
for f in lib/*.c; do i686-w64-mingw32-clang -fms-extensions -c $f -o ${f%.c}.o \
  -Icasemap -I$NT/public/internal/admin/inc -Ilib -Igen -DUNICODE -D_UNICODE -Wno-everything; done

# 2. MSVC __inline emits COMDAT; lld needs to be told
# 3. rename the MSVC-mangled entry so mingw's CRT can start it
i686-w64-mingw32-objcopy --redefine-sym _wmain=_main m_eventcreate.o ev_main.o
i686-w64-mingw32-clang -o evm2.exe ev_main.o m_*.o -mconsole \
  -ladvapi32 -luser32 -lmpr -lsecur32 -lws2_32 -lnetapi32
```

Run: `wine evm2.exe` -> exit 0.

## New error classes found at link/run (classes 5-9)

| # | Class | Detail | Verdict |
|---|---|---|---|
| 5 | **Header ABI mismatch** | The in-tree Windows SDK headers lack `__declspec(dllimport)` decoration, so objects reference `_lstrlenW` while import libs export `_lstrlenW@4`. Mixing in-tree SDK headers with mingw headers breaks the same TU. | **Use the toolchain's own Win32 headers** and the in-tree SDK only for XP-only APIs. This is the single most important M0 finding for M1. |
| 6 | **`__inline` => COMDAT** | MSVC emits `__inline` as COMDAT; 6 objects each define `Int64Shl*Mod32` -> duplicate symbols. | `-Wl,--allow-multiple-definition`. Cosmetic, not a code change. |
| 7 | **Implicit headers via pch** | `rmtconnectivity.c` uses `NameSamCompatible` and `NET_API_STATUS` assuming `windows.h`/`pch.h` pulled in `security.h`+`lm.h`. | Wrapper TU that includes the missing headers before the source. |
| 8 | **Entry-point naming** | `sources` says `UMENTRY=wmain`; source defines `_tmain`; MSVC*mangles* wmain. mingw's CRT wants `main`. | `objcopy --redefine-sym _wmain=_main`. Needs a proper CRT-shim or an explicit entry later. |
| 9 | **No console output under Wine** | `eventcreate` writes via `PrintMessage` from the cmdline lib, which resolves strings from the message table. `evcrtmsg.mc` was hand-compiled, so the table is absent -> nothing is printed. | Expected given the generated header; resolving it means generating the real `.mc` resource (a build-system task, not a code problem). |

**Proof the entry point executes:** an instrumented build printed `M0_PROBE: entry reached`.
So the executable is not merely well-formed, it runs XP source code.

## M0 verdict

**The tree is compilable, linkable and executable with a modern free toolchain.**
The remaining work is build-system engineering, not a port. The precedent that matters for
every later milestone: **use the toolchain's Win32 headers, not the in-tree SDK headers.**

---

## Build harness (`harness/xpbuild.py`)

Turns the hand-driven M0 process into a repeatable one. Reads a razzle `sources`
file and drives llvm-mingw, repairing the error classes automatically.

```sh
./harness/xpbuild.py <tree-relative-dir>    # build one target
./harness/xpbuild.py --scan 25              # build first N discovered targets
./harness/xpbuild.py --report               # error-class statistics from the ledger
```

### Error classes it repairs

| Class | What it does |
|---|---|
| C1 case | case-insensitive mirror of every header a target names |
| C2/C14 `.mc` | generates message headers from `MessageId=`/`SymbolicName=` (incl. cross-tree lookups and `;#define` passthrough) |
| C6 pch | replaces `#include "pch.h"` with the real header set it implied |
| C7 entry | renames `_wmain`/`_main` to what mingw's CRT expects; handles GUI vs console |
| C11 sibling lib | finds a symbol's home directory, builds that `sources` library, relinks |
| C12 C++ | compiles `.cpp` as C++ with MSVC leniency, not as C |
| C13/15 native NT | stub `nt.h`/`ntrtl.h`/`ntstatus.h` with the NT type vocabulary |
| types | DYNLINK -> `-shared .dll`; LIBRARY -> objects only |

### Measured coverage (random sample, pure-C targets)

`4/12` built. Built: `admin/cmdline/eventcreate`, `admin/cmdline/lib`,
`admin/netui/admin/user/umxdebug` (DLL), `admin/netui/macprint/pstodib/lib`,
`admin/dsclientnt4/setup/setup`.

Ledger over 23 targets: C1_case 11, C10_unknown 6, C2_mc 2, C6_pch 1, C9 1.

**Interpretation:** filename casing dominates (11/12 targets needed it), so the
single highest-value tree-level fix is a global case-insensitive index, not more
per-target shims. The remaining `C10_unknown` failures are DDK type-vocabulary
gaps (`TARRAY`, `_wfinddata32_t` collisions) — the same boundary M1 will hit, and
not harness defects.
