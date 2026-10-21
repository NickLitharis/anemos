#!/usr/bin/env bash
# P4: Winsock address layer — byte order, IPv4 text, sockaddr_in.
#
# Checks, each able to fail:
#   1  the pinned clang builds the probe as an i686 PE and its self-tests pass
#      under Wine (byte order, layout, parse/format edges)
#   2  the differential corpus matches an independent Python oracle
#      (ipaddress), including malformed and whitespace forms
#   3  a deliberately broken build is rejected by the same corpus
#   4  the unit stays inside the 8 KB per-TU .text budget
#
# XP's legacy parsing quirks are not claimed here: this unit implements the
# strict POSIX/RFC form. The in-guest probe that settles XP's behaviour is
# part of P4's conformance suite, not this gate.
#
# usage: anemos/winsock/run.sh
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

step "1: build and self-tests"
if clang --target=i686-w64-mingw32 "${FLAGS[@]}" \
        -o "$OUT/ws_probe.exe" "$HERE/addr.c" "$HERE/ws_probe.c"; then
    ok "probe builds"
    if (cd "$OUT" && wine ws_probe.exe selftest) 2>&1 | tr -d '\r' \
            | tee "$OUT/selftest.log"; then
        ok "self-tests pass"
    else
        bad "self-tests pass"
    fi
else
    bad "probe builds"
    printf '\n%d passed, %d failed\n' "$pass" "$fail"
    exit 1
fi

step "2: differential corpus against the independent oracle"
python3 "$HERE/gen_cases.py" >"$OUT/cases.txt"
python3 "$HERE/ws_ref.py" "$OUT/cases.txt" >"$OUT/want.txt"
if (cd "$OUT" && wine ws_probe.exe) <"$OUT/cases.txt" 2>/dev/null \
        | tr -d '\r' >"$OUT/got.txt"; then
    if diff -u "$OUT/want.txt" "$OUT/got.txt" >"$OUT/corpus.diff"; then
        ok "every corpus case matches the oracle"
    else
        bad "every corpus case matches the oracle"
        head -20 "$OUT/corpus.diff" | sed 's/^/    /'
    fi
else
    bad "every corpus case matches the oracle (probe failed)"
fi

step "3: negative control — a wrong renderer must be rejected"
if clang --target=i686-w64-mingw32 "${FLAGS[@]}" -DANEM_WS_NEGATIVE \
        -o "$OUT/ws_probe_neg.exe" "$HERE/addr.c" "$HERE/ws_probe.c"; then
    ok "negative probe builds"
    (cd "$OUT" && wine ws_probe_neg.exe) <"$OUT/cases.txt" 2>/dev/null \
        | tr -d '\r' >"$OUT/got_neg.txt"
    if diff -q "$OUT/want.txt" "$OUT/got_neg.txt" >/dev/null 2>&1; then
        bad "negative renderer is rejected"
    else
        ok "negative renderer is rejected"
    fi
else
    bad "negative probe builds"
fi

step "4: size budget"
if clang --target=i686-w64-mingw32 "${FLAGS[@]}" -c \
        -o "$OUT/addr.obj" "$HERE/addr.c"; then
    text_hex=$(llvm-objdump -h "$OUT/addr.obj" \
        | awk '$2 == ".text" { print $3 }')
    if [ -n "$text_hex" ]; then
        text=$((16#$text_hex))
        printf 'addr.c .text: %s bytes\n' "$text"
        if [ "$text" -le 8192 ]; then
            ok "addr.c fits the 8 KB .text budget"
        else
            bad "addr.c fits the 8 KB .text budget"
        fi
    else
        bad "addr.c fits the 8 KB .text budget (no .text section)"
    fi
else
    bad "addr.c compiles to an object"
fi

printf '\n%d passed, %d failed\n' "$pass" "$fail"
[ "$fail" -eq 0 ] || exit 1
exit 0
