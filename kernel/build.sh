#!/bin/sh
# Builds XP kernel source files freestanding, using kernel/inc/*.h as the
# environment. Output: one .o per source plus a combined libxprtl.a.
set -e
TC="$HOME/.local/opt/llvm-mingw-20261006-msvcrt-ubuntu-22.04-x86_64/bin"
OUT="${1:-./out}"
HERE=$(cd "$(dirname "$0")" && pwd)
CFLAGS="-ffreestanding -fno-stack-protector -fno-builtin -fms-extensions -Wno-everything"
mkdir -p "$OUT"
OBJS=""
for f in "$HERE"/rtl_*.c "$HERE"/src/*.c; do
  [ -f "$f" ] || continue
  b=$(basename "$f" .c)
  "$TC"/i686-w64-mingw32-clang --target=i386-none-elf -c "$f" -o "$OUT/$b.o" \
      -I "$HERE/inc" $CFLAGS
  OBJS="$OBJS $OUT/$b.o"
  printf '  built %s.o\n' "$b"
done
ar rcs "$OUT/libxprtl.a" $OBJS 2>/dev/null || true
echo "libxprtl.a: $(ls -l "$OUT/libxprtl.a" | awk '{print $5}') bytes"
