# v0.1 — bootable image

**Status: PASSING.** A freestanding i386 image built here boots under QEMU and
emits an identifiable string over the debug console (port 0xE9).

```
$ cd boot && ./build.sh ./out
built ./out/xp-v01.elf

$ qemu-system-i386 -kernel ./out/xp-v01.elf -display none -debugcon file:./out/con.txt
$ cat ./out/con.txt
XP-MODERNIZATION-V0.1: harness binary executing under QEMU
```

## What this proves

- The toolchain that builds the XP tree can also produce a bootable image.
- QEMU boots our image, transfers control to our code, and our code executes.
- `-kernel` requires a multiboot header; the checksum is validated by QEMU, so a
  wrong header fails loudly instead of silently doing nothing.
- The verification is falsifiable: a broken image produces no output, and an
  incorrect checksum makes QEMU refuse the file outright.

## What this does not prove

That the XP kernel itself boots. The XP boot chain needs NT loader structures,
the real HAL, and DDK internals the tree does not fully provide. Getting from
here to the real kernel is the M1 problem described in MODERNIZATION_PLAN.md.

## Files

| File | Purpose |
|---|---|
| `hello.c` | freestanding entry, prints via outb to 0xE9, then halts |
| `mb.c` | multiboot v1 header (magic, flags, checksum) |
| `link.ld` | links at 0x100000 with `.multiboot` first |
| `build.sh` | builds the image with the same llvm-mingw that builds XPSP1 |
