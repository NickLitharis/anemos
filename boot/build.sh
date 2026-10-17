#!/bin/sh
# v0.3: bootable i386 image whose payload is REAL XP SP1 kernel source:
#   base/ntos/rtl/byteswap.c, intbits.c, checksum.c, slistfunc.c
# Each routine's behaviour is asserted inside the image.
set -e
TC="$HOME/.local/opt/llvm-mingw-20261006-msvcrt-ubuntu-22.04-x86_64/bin"
OUT="${1:-./out}"
CFLAGS="-ffreestanding -fno-stack-protector -fno-builtin -Wno-everything"
mkdir -p "$OUT"
"$TC"/i686-w64-mingw32-clang --target=i386-none-elf -c hello_rtl2.c -o "$OUT/hello_rtl2.o" $CFLAGS
for f in byteswap intbits checksum slistfunc guid; do
  "$TC"/i686-w64-mingw32-clang --target=i386-none-elf -c "rtl/${f}_standalone.c" -o "$OUT/$f.o" $CFLAGS
done
"$TC"/i686-w64-mingw32-clang --target=i386-none-elf -c mb.c -o "$OUT/mb.o" $CFLAGS
ld.lld -T link.ld "$OUT"/hello_rtl2.o "$OUT"/byteswap.o "$OUT"/intbits.o \
        "$OUT"/checksum.o "$OUT"/slistfunc.o "$OUT"/guid.o "$OUT"/mb.o -o "$OUT/xp-v04.elf"
echo "built $OUT/xp-v04.elf"
