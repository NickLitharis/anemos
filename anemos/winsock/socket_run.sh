#!/usr/bin/env bash
# P4.2: socket lifecycle core — states, requests, backlog, shutdown.
#
# Checks, each able to fail:
#   1  the pinned clang builds the trace harness and a fixed set of scenarios
#      plus seeded random walks runs under Wine
#   2  for every trace, the C core and an independent Python model produce
#      identical lines (operations, results, and the drained request queue)
#   3  a build with a broken accept queue is rejected by the same corpus
#   4  the core stays inside the 8 KB per-TU .text budget
#
# The core owns no socket: the transport is represented by the request queue
# and injected events, which is what makes the traces deterministic.
#
# usage: anemos/winsock/socket_run.sh
set -uo pipefail

HERE=$(cd "$(dirname "$0")" && pwd)
OUT="$HERE/out"
TRACES="$OUT/traces"
LLVM="$HOME/.local/opt/llvm-mingw-20261006-msvcrt-ubuntu-22.04-x86_64/bin"

export PATH="$LLVM:$PATH"
export WINEDEBUG=-all WINEPREFIX=${WINEPREFIX:-$HOME/.wine-xp}

FLAGS=(-std=c23 -Oz -Wall -Wextra -Werror -Wconversion -Wshadow -Wcast-align
       -Wstrict-prototypes -Wmissing-prototypes -Wnull-dereference)
SOURCES=("$HERE/addr.c" "$HERE/socket_core.c" "$HERE/socket_probe.c")

pass=0
fail=0
ok()  { pass=$((pass + 1)); printf 'PASS  %s\n' "$1"; }
bad() { fail=$((fail + 1)); printf 'FAIL  %s\n' "$1"; }
step() { printf '\n== %s ==\n' "$1"; }

[ -d "$WINEPREFIX" ] || wineboot -i >/dev/null 2>&1
mkdir -p "$OUT" "$TRACES"

step "1: build the trace harness"
if clang --target=i686-w64-mingw32 "${FLAGS[@]}" \
        -o "$OUT/socket_probe.exe" "${SOURCES[@]}"; then
    ok "socket probe builds"
else
    bad "socket probe builds"
    printf '\n%d passed, %d failed\n' "$pass" "$fail"
    exit 1
fi

python3 "$HERE/gen_traces.py" "$TRACES"
trace_count=$(find "$TRACES" -name '*.txt' | wc -l)

step "2: differential traces against the independent model"
mismatches=0
checked=0
for trace in "$TRACES"/*.txt; do
    name=$(basename "$trace" .txt)
    python3 "$HERE/socket_ref.py" "$trace" >"$OUT/want-$name.txt"
    (cd "$OUT" && wine socket_probe.exe) <"$trace" 2>/dev/null \
        | tr -d '\r' >"$OUT/got-$name.txt"
    checked=$((checked + 1))
    if ! diff -u "$OUT/want-$name.txt" "$OUT/got-$name.txt" \
            >"$OUT/diff-$name.txt"; then
        mismatches=$((mismatches + 1))
        printf '  mismatch: %s\n' "$name"
        head -12 "$OUT/diff-$name.txt" | sed 's/^/    /'
    fi
done
if [ "$checked" -ge 20 ] && [ "$mismatches" -eq 0 ]; then
    ok "$checked traces match the independent model"
else
    bad "$checked traces match the independent model ($mismatches mismatched)"
fi

step "3: harness robustness"
python3 -c "print('select\\t' + ','.join(['1'] * 700))" >"$OUT/overlong.txt"
(cd "$OUT" && wine socket_probe.exe) <"$OUT/overlong.txt" 2>/dev/null \
    | tr -d '\r' >"$OUT/overlong-got.txt"
if [ "$(cat "$OUT/overlong-got.txt")" = "? overlong" ]; then
    ok "an overlong line is rejected, not split"
else
    bad "an overlong line is rejected, not split"
fi
sed 's/$/\r/' "$TRACES/readiness.txt" >"$OUT/crlf-trace.txt"
python3 "$HERE/socket_ref.py" "$OUT/crlf-trace.txt" >"$OUT/crlf-want.txt"
(cd "$OUT" && wine socket_probe.exe) <"$OUT/crlf-trace.txt" 2>/dev/null \
    | tr -d '\r' >"$OUT/crlf-got.txt"
if diff -q "$OUT/crlf-want.txt" "$OUT/crlf-got.txt" >/dev/null 2>&1; then
    ok "a CRLF trace produces the same result"
else
    bad "a CRLF trace produces the same result"
fi

step "4: negative control — a broken accept queue must be rejected"
if clang --target=i686-w64-mingw32 "${FLAGS[@]}" -DANEM_SOCKET_NEGATIVE \
        -o "$OUT/socket_probe_neg.exe" "${SOURCES[@]}"; then
    ok "negative probe builds"
    negative_mismatches=0
    for trace in "$TRACES"/*.txt; do
        name=$(basename "$trace" .txt)
        python3 "$HERE/socket_ref.py" "$trace" >"$OUT/negwant-$name.txt"
        (cd "$OUT" && wine socket_probe_neg.exe) <"$trace" 2>/dev/null \
            | tr -d '\r' >"$OUT/neggot-$name.txt"
        if ! diff -q "$OUT/negwant-$name.txt" "$OUT/neggot-$name.txt" \
                >/dev/null 2>&1; then
            negative_mismatches=$((negative_mismatches + 1))
        fi
    done
    if [ "$negative_mismatches" -gt 0 ]; then
        ok "broken accept queue is rejected ($negative_mismatches traces)"
    else
        bad "broken accept queue is rejected"
    fi
else
    bad "negative probe builds"
fi

step "5: size budget"
if clang --target=i686-w64-mingw32 "${FLAGS[@]}" -c \
        -o "$OUT/socket_core.obj" "$HERE/socket_core.c"; then
    text_hex=$(llvm-objdump -h "$OUT/socket_core.obj" \
        | awk '$2 == ".text" { print $3 }')
    if [ -n "$text_hex" ]; then
        text=$((16#$text_hex))
        printf 'socket_core.c .text: %s bytes\n' "$text"
        if [ "$text" -le 8192 ]; then
            ok "socket_core.c fits the 8 KB .text budget"
        else
            bad "socket_core.c fits the 8 KB .text budget"
        fi
    else
        bad "socket_core.c fits the 8 KB .text budget (no .text section)"
    fi
else
    bad "socket_core.c compiles to an object"
fi

printf '\n%d passed, %d failed\n' "$pass" "$fail"
[ "$fail" -eq 0 ] || exit 1
exit 0
