#!/usr/bin/env bash
# P4.5: the transport seam and its loopback reference transport.
#
# Checks, each able to fail:
#   1  the loopback scenarios build and every assertion passes under Wine
#      (TCP connect/accept/echo, EOF on shutdown and close, refusal, backlog
#      overflow, UDP both directions)
#   2  the corpus is not vacuous: it must actually report its checks
#   3  a negative build that drops a byte in transit is rejected
#   4  loopback.c and socket_core.c stay inside the 8 KB per-TU budget
#
# The seam is what a real provider must implement; the loopback replaces only
# the bottom half. lwIP-vs-ReactOS remains a separate, still-open decision.
#
# usage: anemos/winsock/transport_run.sh
set -uo pipefail

HERE=$(cd "$(dirname "$0")" && pwd)
OUT="$HERE/out"
LLVM="$HOME/.local/opt/llvm-mingw-20261006-msvcrt-ubuntu-22.04-x86_64/bin"

export PATH="$LLVM:$PATH"
export WINEDEBUG=-all WINEPREFIX=${WINEPREFIX:-$HOME/.wine-xp}

FLAGS=(-std=c23 -Oz -Wall -Wextra -Werror -Wconversion -Wshadow -Wcast-align
       -Wstrict-prototypes -Wmissing-prototypes -Wnull-dereference -I "$HERE")
SOURCES=("$HERE/addr.c" "$HERE/socket_core.c" "$HERE/loopback.c"
         "$HERE/transport_probe.c")

pass=0
fail=0
ok()  { pass=$((pass + 1)); printf 'PASS  %s\n' "$1"; }
bad() { fail=$((fail + 1)); printf 'FAIL  %s\n' "$1"; }
step() { printf '\n== %s ==\n' "$1"; }

[ -d "$WINEPREFIX" ] || wineboot -i >/dev/null 2>&1
mkdir -p "$OUT"

step "1: build and run the loopback scenarios"
if clang --target=i686-w64-mingw32 "${FLAGS[@]}" \
        -o "$OUT/transport_probe.exe" "${SOURCES[@]}"; then
    ok "loopback probe builds"
    if (cd "$OUT" && timeout 60 wine transport_probe.exe) 2>&1 | tr -d '\r' \
            | tee "$OUT/transport.log"; then
        ok "every loopback scenario passes"
    else
        bad "every loopback scenario passes"
    fi
else
    bad "loopback probe builds"
    printf '\n%d passed, %d failed\n' "$pass" "$fail"
    exit 1
fi

step "2: the scenario suite is not vacuous"
assertions=$(grep -c '^PASS ' "$OUT/transport.log" || true)
if [ "$assertions" -ge 20 ] \
   && grep -q '^all loopback transport checks pass$' "$OUT/transport.log"; then
    ok "suite reports $assertions assertions and its final banner"
else
    bad "suite reports $assertions assertions and its final banner"
fi

step "3: negative control — a dropped byte must be caught"
if clang --target=i686-w64-mingw32 "${FLAGS[@]}" -DANEM_LOOPBACK_NEGATIVE \
        -o "$OUT/transport_probe_neg.exe" "${SOURCES[@]}"; then
    ok "negative probe builds"
    if (cd "$OUT" && timeout 60 wine transport_probe_neg.exe) >/dev/null 2>&1; then
        bad "negative probe is rejected"
    else
        ok "negative probe is rejected"
    fi
else
    bad "negative probe builds"
fi

step "4: size budgets"
if clang --target=i686-w64-mingw32 "${FLAGS[@]}" -c \
        -o "$OUT/loopback.obj" "$HERE/loopback.c"; then
    text_hex=$(llvm-objdump -h "$OUT/loopback.obj" \
        | awk '$2 == ".text" { print $3 }')
    text=$((16#${text_hex:-0}))
    printf 'loopback.c .text: %s bytes\n' "$text"
    if [ "$text" -gt 0 ] && [ "$text" -le 8192 ]; then
        ok "loopback.c fits the 8 KB .text budget"
    else
        bad "loopback.c fits the 8 KB .text budget"
    fi
else
    bad "loopback.c compiles to an object"
fi

if clang --target=i686-w64-mingw32 "${FLAGS[@]}" -c \
        -o "$OUT/socket_core.obj" "$HERE/socket_core.c"; then
    text_hex=$(llvm-objdump -h "$OUT/socket_core.obj" \
        | awk '$2 == ".text" { print $3 }')
    text=$((16#${text_hex:-0}))
    printf 'socket_core.c .text: %s bytes\n' "$text"
    if [ "$text" -gt 0 ] && [ "$text" -le 8192 ]; then
        ok "socket_core.c fits the 8 KB .text budget"
    else
        bad "socket_core.c fits the 8 KB .text budget"
    fi
else
    bad "socket_core.c compiles to an object"
fi

printf '\n%d passed, %d failed\n' "$pass" "$fail"
[ "$fail" -eq 0 ] || exit 1
exit 0
