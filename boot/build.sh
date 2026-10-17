#!/bin/sh
# v0.1 bootable image: freestanding i386 code built with the same toolchain the
# XP tree uses, linked with a multiboot header, booted in QEMU.
# Prints over the QEMU debug console (port 0xE9) -- no OS services needed.
set -e
TC="$HOME/.local/opt/llvm-mingw-20261006-msvcrt-ubuntu-22.04-x86_64/bin"
OUT="${1:-./out}"
mkdir -p "$OUT"
"$TC"/i686-w64-mingw32-clang --target=i386-none-elf -c hello.c -o "$OUT/hello.o" -ffreestanding -fno-stack-protector
"$TC"/i686-w64-mingw32-clang --target=i386-none-elf -c mb.c -o "$OUT/mb.o" -ffreestanding
ld.lld -T link.ld "$OUT/hello.o" "$OUT/mb.o" -o "$OUT/xp-v01.elf"
echo "built $OUT/xp-v01.elf"
