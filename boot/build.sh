#!/bin/sh
# Builds a bootable image containing XP kernel source + a test harness.
# Usage: ./build.sh <outdir> [testfile]
set -e
TC="$HOME/.local/opt/llvm-mingw-20261006-msvcrt-ubuntu-22.04-x86_64/bin"
OUT="${1:-./out}"
TEST="${2:-../kernel/tests/test_bitmap.c}"
CFLAGS="-ffreestanding -fno-stack-protector -fno-builtin -Wno-everything"
mkdir -p "$OUT"
"$TC"/i686-w64-mingw32-clang --target=i386-none-elf -c "$TEST" -o "$OUT/test.o" $CFLAGS
"$TC"/i686-w64-mingw32-clang --target=i386-none-elf -c mb.c -o "$OUT/mb.o" $CFLAGS
../kernel/build.sh "$OUT/xprtl" >/dev/null
ld.lld -T link.ld --allow-multiple-definition "$OUT/test.o" "$OUT/xprtl/libxprtl.a" "$OUT/mb.o" -o "$OUT/xp-test.elf"
echo "built $OUT/xp-test.elf"
