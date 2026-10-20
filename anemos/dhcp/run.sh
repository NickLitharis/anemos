#!/bin/bash
# P2: DHCP codec (RFC 2131/2132), gated four ways. Each can fail:
#
#   1  the pinned toolchain builds the vector suite as an i686 PE and it
#      passes under Wine
#   2  the same suite built with a weakened option-length check FAILS
#      — a check that cannot fail is not a check
#   3  a native ASan/UBSan build classifies a generated corpus identically to
#      dhcp_ref.py, an independent implementation: same status codes, same
#      bytes (parse -> write is exact), same reparse status
#   4  dhcp.c compiles warning-free freestanding for i686-windows-msvc and
#      stays inside the per-TU size budget
#
# usage: anemos/dhcp/run.sh        (exit 0 = this unit passes)
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
        -o "$OUT/dhcptest.exe" "$HERE/dhcp.c" "$HERE/test_dhcp.c"; then
    ok "clang builds the suite"
    if wine "$OUT/dhcptest.exe" 2>&1 | tr -d '\r' | tee "$OUT/dhcptest.log"; then
        ok "vector suite passes"
    else
        bad "vector suite passes"
    fi
else
    bad "clang builds the suite"
fi

step "2: negative control — a weakened option check must fail the suite"
if clang --target=i686-w64-mingw32 "${FLAGS[@]}" -DANEM_DHCP_NEGATIVE \
        -o "$OUT/dhcpneg.exe" "$HERE/dhcp.c" "$HERE/test_dhcp.c"; then
    ok "weakened build compiles"
    if wine "$OUT/dhcpneg.exe" >/dev/null 2>&1; then
        bad "weakened build is rejected"
    else
        ok "weakened build is rejected"
    fi
else
    bad "weakened build compiles"
fi

step "3: differential corpus against dhcp_ref.py (independent implementation)"
if python3 "$HERE/dhcp_ref.py" --check >"$OUT/ref.log" 2>&1; then
    ok "reference self-check (golden vectors + malformed classes)"
else
    bad "reference self-check"
    sed 's/^/    /' "$OUT/ref.log"
fi

if python3 "$HERE/dhcp_ref.py" --emit "$OUT" >"$OUT/emit.log" 2>&1; then
    ok "corpus generated"
    if /usr/bin/clang -std=c2x -O1 -g -fsanitize=address,undefined \
            -fno-omit-frame-pointer -Wall -Wextra -Werror \
            -o "$OUT/dhcpcorpus" "$HERE/dhcp.c" "$HERE/corpus_main.c"; then
        ok "native sanitised corpus driver builds"
        if "$OUT/dhcpcorpus" "$OUT/corpus.bin" >"$OUT/got.txt" 2>"$OUT/corpus.err"; then
            ok "corpus driver ran"
            if diff -u "$OUT/expected.txt" "$OUT/got.txt" >"$OUT/diff.txt"; then
                ok "C and Python agree on every corpus case"
            else
                bad "C and Python agree on every corpus case"
                head -40 "$OUT/diff.txt" | sed 's/^/    /'
            fi
        else
            bad "corpus driver ran"
            sed 's/^/    /' "$OUT/corpus.err"
        fi
        if /usr/bin/clang -std=c2x -O1 -g -DANEM_DHCP_NEGATIVE \
                -Wall -Wextra -Werror \
                -o "$OUT/dhcpcorpusneg" "$HERE/dhcp.c" "$HERE/corpus_main.c"; then
            if "$OUT/dhcpcorpusneg" "$OUT/corpus.bin" >"$OUT/gotneg.txt" 2>/dev/null \
               && ! diff -q "$OUT/expected.txt" "$OUT/gotneg.txt" >/dev/null; then
                ok "weakened codec is rejected by the corpus"
            else
                bad "weakened codec is rejected by the corpus"
            fi
        else
            bad "weakened corpus driver builds"
        fi
    else
        bad "native sanitised corpus driver builds"
    fi
else
    bad "corpus generated"
    sed 's/^/    /' "$OUT/emit.log"
fi

step "4: freestanding i686-windows-msvc object + size budget"
if clang --target=i686-windows-msvc -ffreestanding -fno-builtin \
        -fno-stack-protector "${FLAGS[@]}" \
        -c "$HERE/dhcp.c" -o "$OUT/dhcp.i686.obj"; then
    ok "dhcp.c is freestanding-clean for i686-windows-msvc"
    hex=$(llvm-objdump -h "$OUT/dhcp.i686.obj" 2>/dev/null \
           | awk '$2 == ".text" { print $3; exit }')
    text=$(printf '%d' "0x$hex" 2>/dev/null)
    if [ -z "${hex:-}" ] || [ -z "${text:-}" ]; then
        bad "dhcp.c .text size readable"
    elif [ "$text" -le 8192 ]; then
        ok "dhcp.c .text is ${text} bytes (budget 8192)"
    else
        bad "dhcp.c .text is ${text} bytes (budget 8192)"
    fi
else
    bad "dhcp.c is freestanding-clean for i686-windows-msvc"
fi

printf '\n%d passed, %d failed\n' "$pass" "$fail"
[ "$fail" -eq 0 ] || exit 1
exit 0
