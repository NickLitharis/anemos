# v0.2 — real XP SP1 source executing under QEMU

```
$ cd boot && ./build.sh ./out
built ./out/xp-v02.elf

$ qemu-system-i386 -kernel ./out/xp-v02.elf -display none -debugcon file:./out/con2.txt
$ cat ./out/con2.txt
RtlUshortByteSwap(1234)=3412 OK
RtlUlongByteSwap(12345678)=78563412 OK
RtlUlonglongByteSwap(..55667788) low32=44332211 OK
XP-MODERNIZATION-V0.2: XP SP1 RTL CODE EXECUTED UNDER QEMU
```

## What is proven

Three routines from `Source/XPSP1/NT/base/ntos/rtl/byteswap.c` — **XP kernel
source, routine bodies unchanged** — are compiled freestanding, linked into a
bootable image, and executed by QEMU. Their return values are asserted against
known-correct results inside the image, so a wrong answer prints FAIL and would
not be silenced.

`boot/rtl/byteswap_standalone.c` is the XP file with only the two `#include`
lines replaced by a minimal type prelude; the function bodies are byte-identical
to the tree.

## Two real bugs this caught

1. **Calling convention.** The XP routines are `FASTCALL` (`__attribute__((fastcall))`
   under GCC/Clang), so a cdecl prototype makes the caller push the argument on
   the stack while the callee reads `%cx` — garbage in, wrong answer out. The
   harness's generated headers must carry the calling convention, not just the type.
2. **A wrong assertion of mine, not wrong XP code.** `RtlUlonglongByteSwap(0x1122334455667788)`
   is `0x8877665544332211`, so the low 32 bits are `0x44332211`. I had asserted
   `0x88776655` and the run failed — the correct outcome for a bad test.

## Why this matters for the plan

This is the first time **XP source runs on modern hardware through our own
toolchain**. The remaining distance to a real kernel is dependency depth, not
principle: `byteswap.c` needed only types, whereas `mm/` and `ke/` need the full
NT internal vocabulary.

## Files

| File | Purpose |
|---|---|
| `hello_rtl.c` | boots, calls the XP routines, asserts results, prints via port 0xE9 |
| `rtl/byteswap_standalone.c` | XP `byteswap.c` with includes replaced by a prelude |
| `mb.c` | multiboot v1 header |
| `link.ld` | links at 0x100000, `.multiboot` first |
| `build.sh` | builds the image with the same llvm-mingw that builds XPSP1 |

---

# v0.3 — four XP kernel source files verified under QEMU

```
$ cd boot && ./build.sh ./out
built ./out/xp-v03.elf
$ qemu-system-i386 -kernel ./out/xp-v03.elf -display none -debugcon file:./out/con3.txt
$ cat ./out/con3.txt
SetBits.ret=0 OK
SetBits.val=10 OK
ClearBits.ret=10 OK
ClearBits.val=0 OK
Crc32(123456789)=CBF43926 OK
SList.depth=0 OK
XP-MODERNIZATION-V0.3: 3 MORE XP RTL FILES EXECUTED UNDER QEMU
```

XP source files now executing (bodies unchanged from the tree):

| File | Routines exercised | Assertion |
|---|---|---|
| `base/ntos/rtl/byteswap.c` | RtlUshort/Ulong/UlonglongByteSwap | 3 exact-value checks |
| `base/ntos/rtl/intbits.c` | RtlInterlockedSetBits/ClearBits | return-previous-value + resulting flags |
| `base/ntos/rtl/checksum.c` | RtlComputeCrc32 | standard CRC-32 of "123456789" = 0xCBF43926 |
| `base/ntos/rtl/slistfunc.c` | RtlpInitializeSListHead/QueryDepthSList | depth reads back 0 |

The CRC-32 check is the strongest one: `0xCBF43926` is the published check value
for that test vector, so a subtly wrong bit in XP's table or loop shows up.

## Needs the harness must meet (learned here)

- **`FASTCALL` in prototypes.** These routines are fastcall; cdecl callers read
  garbage. Any generated header must carry the calling convention.
- **`RTL_PAGED_CODE()` and `InterlockedCompareExchange`** are kernel-runtime
  macros/imports the sources assume. A freestanding build must supply them.
- **Doc-comment layout.** XP puts the parameter list before its `/*++ … --*/`
  block; naive preprocessing that strips comments can swallow a function header.

---

# v0.4 — five XP kernel source files verified (adds guid.c)

```
$ cd boot && ./build.sh ./out
built ./out/xp-v04.elf
$ qemu-system-i386 -kernel ./out/xp-v04.elf -display none -debugcon file:./out/con4.txt
$ cat ./out/con4.txt
SetBits.ret=0 OK
SetBits.val=10 OK
ClearBits.ret=10 OK
ClearBits.val=0 OK
Crc32(123456789)=CBF43926 OK
SList.depth=0 OK
GUIDFromString.status=0 OK
GUID.Data1=12345678 OK
XP-MODERNIZATION-V0.4: 5 XP SP1 KERNEL SOURCE FILES EXECUTED AND VERIFIED
```

`base/ntos/rtl/guid.c` is new here: `RtlGUIDFromString` parses
`{12345678-1234-5678-9abc-def012345678}` through XP's own `ScanHexFormat`
and yields `Data1 = 0x12345678` — a real state machine, not a constant.

## Debugging notes worth keeping

- **Wide literals are unsafe on `i386-none-elf`.** `L"…"` is `int`-sized here, so
  assigning it to a `WCHAR` (`unsigned short`) array silently stored zeros. Build
  wide buffers by code point instead.
- **`GuidFormat` must include the braces.** The parser treats every non-`%`
  character as a literal to match, so a format without `{`/`}` rejects the
  standard GUID text with `STATUS_INVALID_PARAMETER` (`0xC000000D`).
- **These failure modes are exactly the kind that produce silently-wrong
  results.** Both were caught only because the image asserts on values.
