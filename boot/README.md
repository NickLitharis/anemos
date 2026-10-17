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
