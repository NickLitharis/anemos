#!/bin/bash
# P1, first unit: MD5, gated on RFC 1321's published vectors.
#
# Three checks, each of which can fail:
#   1  the pinned toolchain builds the suite as an i686 PE and it passes under Wine
#   2  the same suite built against a deliberately corrupted transform FAILS —
#      a check that cannot fail is not a check
#   3  md5.c compiles warning-free freestanding for the real destination
#      (i686-windows-msvc), which is where a replacement gets linked
#
# usage: anemos/md5/run.sh        (exit 0 = this unit passes)
set -uo pipefail

HERE=$(cd "$(dirname "$0")" && pwd)
OUT="$HERE/out"
LLVM="$HOME/.local/opt/llvm-mingw-20261006-msvcrt-ubuntu-22.04-x86_64/bin"

export PATH="$LLVM:$PATH"
export WINEDEBUG=-all WINEPREFIX=${WINEPREFIX:-$HOME/.wine-xp}

FLAGS=(-std=c23 -Oz -Wall -Wextra -Werror -Wconversion -Wshadow -Wcast-align
       -Wstrict-prototypes -Wmissing-prototypes -Wnull-dereference)

pass=0
fail=0
ok()  { pass=$((pass + 1)); printf 'PASS  %s\n' "$1"; }
bad() { fail=$((fail + 1)); printf 'FAIL  %s\n' "$1"; }
step() { printf '\n== %s ==\n' "$1"; }

[ -d "$WINEPREFIX" ] || wineboot -i >/dev/null 2>&1
mkdir -p "$OUT"

step "1: vector suite (pinned clang, i686 PE, Wine)"
if clang --target=i686-w64-mingw32 "${FLAGS[@]}" \
        -o "$OUT/md5test.exe" "$HERE/md5.c" "$HERE/test_md5.c"; then
    ok "clang builds the suite"
    if wine "$OUT/md5test.exe" 2>&1 | tr -d '\r' | tee "$OUT/md5test.log"; then
        ok "vector suite passes"
    else
        bad "vector suite passes"
    fi
else
    bad "clang builds the suite"
fi

step "2: negative control — a corrupted transform must fail the suite"
if clang --target=i686-w64-mingw32 "${FLAGS[@]}" -DANEM_MD5_NEGATIVE \
        -o "$OUT/md5neg.exe" "$HERE/md5.c" "$HERE/test_md5.c"; then
    ok "corrupted build compiles"
    if wine "$OUT/md5neg.exe" >/dev/null 2>&1; then
        bad "corrupted build is rejected"
    else
        ok "corrupted build is rejected"
    fi
else
    bad "corrupted build compiles"
fi

step "3: freestanding i686-windows-msvc object (the destination ABI)"
if clang --target=i686-windows-msvc -ffreestanding -fno-builtin \
        -fno-stack-protector "${FLAGS[@]}" \
        -c "$HERE/md5.c" -o "$OUT/md5.i686.obj"; then
    ok "md5.c is freestanding-clean for i686-windows-msvc"
else
    bad "md5.c is freestanding-clean for i686-windows-msvc"
fi

printf '\n%d passed, %d failed\n' "$pass" "$fail"
[ "$fail" -eq 0 ] || exit 1
exit 0
