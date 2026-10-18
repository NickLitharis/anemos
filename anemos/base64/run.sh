#!/bin/bash
# P1: Base64 (RFC 4648), gated on published vectors and strictness rules.
#
# Three checks, each of which can fail:
#   1  the pinned toolchain builds the suite as an i686 PE and it passes under Wine
#   2  the same suite built WITHOUT the canonical-trailing-bits check FAILS —
#      a check that cannot fail is not a check
#   3  base64.c compiles warning-free freestanding for the real destination
#      (i686-windows-msvc), which is where a replacement gets linked
#
# usage: anemos/base64/run.sh        (exit 0 = this unit passes)
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

step "1: vector + strictness suite (pinned clang, i686 PE, Wine)"
if clang --target=i686-w64-mingw32 "${FLAGS[@]}" \
        -o "$OUT/b64test.exe" "$HERE/base64.c" "$HERE/test_base64.c"; then
    ok "clang builds the suite"
    if wine "$OUT/b64test.exe" 2>&1 | tr -d '\r' | tee "$OUT/b64test.log"; then
        ok "suite passes"
    else
        bad "suite passes"
    fi
else
    bad "clang builds the suite"
fi

step "2: negative control — a lenient decoder must fail the suite"
if clang --target=i686-w64-mingw32 "${FLAGS[@]}" -DANEM_B64_NEGATIVE \
        -o "$OUT/b64neg.exe" "$HERE/base64.c" "$HERE/test_base64.c"; then
    ok "lenient build compiles"
    if wine "$OUT/b64neg.exe" >/dev/null 2>&1; then
        bad "lenient build is rejected"
    else
        ok "lenient build is rejected"
    fi
else
    bad "lenient build compiles"
fi

step "3: freestanding i686-windows-msvc object (the destination ABI)"
if clang --target=i686-windows-msvc -ffreestanding -fno-builtin \
        -fno-stack-protector "${FLAGS[@]}" \
        -c "$HERE/base64.c" -o "$OUT/base64.i686.obj"; then
    ok "base64.c is freestanding-clean for i686-windows-msvc"
else
    bad "base64.c is freestanding-clean for i686-windows-msvc"
fi

printf '\n%d passed, %d failed\n' "$pass" "$fail"
[ "$fail" -eq 0 ] || exit 1
exit 0
