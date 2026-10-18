#!/bin/bash
# P1: NLS tables — CP1252 (Unicode Consortium mapping) and strict UTF-8
# (RFC 3629).
#
# Three checks, each of which can fail:
#   1  the pinned toolchain builds the suite as an i686 PE and it passes under Wine
#   2  the same suite built WITHOUT the overlong-form check FAILS —
#      a check that cannot fail is not a check
#   3  nls.c compiles warning-free freestanding for the real destination
#      (i686-windows-msvc), which is where a replacement gets linked
#
# usage: anemos/nls/run.sh        (exit 0 = this unit passes)
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

step "1: table + codec suite (pinned clang, i686 PE, Wine)"
if clang --target=i686-w64-mingw32 "${FLAGS[@]}" \
        -o "$OUT/nlstest.exe" "$HERE/nls.c" "$HERE/test_nls.c"; then
    ok "clang builds the suite"
    if wine "$OUT/nlstest.exe" 2>&1 | tr -d '\r' | tee "$OUT/nlstest.log"; then
        ok "suite passes"
    else
        bad "suite passes"
    fi
else
    bad "clang builds the suite"
fi

step "2: negative control — a decoder without the overlong check must fail"
if clang --target=i686-w64-mingw32 "${FLAGS[@]}" -DANEM_NLS_NEGATIVE \
        -o "$OUT/nlsneg.exe" "$HERE/nls.c" "$HERE/test_nls.c"; then
    ok "lenient build compiles"
    if wine "$OUT/nlsneg.exe" >/dev/null 2>&1; then
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
        -c "$HERE/nls.c" -o "$OUT/nls.i686.obj"; then
    ok "nls.c is freestanding-clean for i686-windows-msvc"
else
    bad "nls.c is freestanding-clean for i686-windows-msvc"
fi

printf '\n%d passed, %d failed\n' "$pass" "$fail"
[ "$fail" -eq 0 ] || exit 1
exit 0
