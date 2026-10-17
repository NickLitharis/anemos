#!/bin/sh
# v0.2: bootable i386 image whose payload includes REAL XP SP1 source
# (base/ntos/rtl/byteswap.c) and verifies its behaviour under QEMU.
set -e
TC="$HOME/.local/opt/llvm-mingw-20261006-msvcrt-ubuntu-22.04-x86_64/bin"
OUT="${1:-./out}"
CFLAGS="-ffreestanding -fno-stack-protector -fno-builtin -Wno-everything"
mkdir -p "$OUT"
"$TC"/i686-w64-mingw32-clang --target=i386-none-elf -c hello_rtl.c -o "$OUT/hello_rtl.o" $CFLAGS
"$TC"/i686-w64-mingw32-clang --target=i386-none-elf -c rtl/byteswap_standalone.c -o "$OUT/byteswap.o" $CFLAGS
"$TC"/i686-w64-mingw32-clang --target=i386-none-elf -c mb.c -o "$OUT/mb.o" $CFLAGS
ld.lld -T link.ld "$OUT/hello_rtl.o" "$OUT/byteswap.o" "$OUT/mb.o" -o "$OUT/xp-v02.elf"
echo "built $OUT/xp-v02.elf"
