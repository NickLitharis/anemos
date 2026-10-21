#!/usr/bin/env bash
# P4.5 spike: lwIP 2.2.0 builds for i686 and its loopback path carries a
# datagram with no OS underneath.
#
# usage: anemos/net/run.sh
set -uo pipefail

HERE=$(cd "$(dirname "$0")" && pwd)
OUT="$HERE/out"
LLVM="$HOME/.local/opt/llvm-mingw-20261006-msvcrt-ubuntu-22.04-x86_64/bin"
LWIP="$HOME/.local/opt/lwip-2.2.0-i686"

export PATH="$LLVM:$PATH"
export WINEDEBUG=-all WINEPREFIX=${WINEPREFIX:-$HOME/.wine-xp}

pass=0
fail=0
ok()  { pass=$((pass + 1)); printf 'PASS  %s\n' "$1"; }
bad() { fail=$((fail + 1)); printf 'FAIL  %s\n' "$1"; }
step() { printf '\n== %s ==\n' "$1"; }

[ -d "$WINEPREFIX" ] || wineboot -i >/dev/null 2>&1
mkdir -p "$OUT"

step "1: the pinned dependency"
if [ -f "$LWIP/lib/liblwip.a" ] && [ -f "$LWIP/include/lwipopts.h" ]; then
    ok "lwIP is built and installed"
else
    bad "lwIP is built and installed (run anemos/net/build-lwip.sh)"
    printf '\n%d passed, %d failed\n' "$pass" "$fail"
    exit 1
fi

step "2: the smoke test"
if i686-w64-mingw32-clang -std=c23 -Oz -Wall -Wextra -Werror -Wno-pragma-pack \
        -I "$LWIP/include" -I "$HERE" \
        -o "$OUT/lwip_smoke.exe" "$HERE/lwip_smoke.c" \
        "$LWIP/lib/liblwip.a" -lws2_32 -Xlinker /subsystem:console,5.01; then
    ok "smoke test builds"
    if (cd "$OUT" && timeout 60 wine lwip_smoke.exe) 2>&1 | tr -d '\r' \
            | tee "$OUT/smoke.log"; then
        ok "loopback datagram passes"
    else
        bad "loopback datagram passes"
    fi
else
    bad "smoke test builds"
fi

step "3: negative control — a wrong payload must be rejected"
if i686-w64-mingw32-clang -std=c23 -Oz -Wall -Wextra -Werror -Wno-pragma-pack \
        -DANEM_LWIP_NEGATIVE -I "$LWIP/include" -I "$HERE" \
        -o "$OUT/lwip_smoke_neg.exe" "$HERE/lwip_smoke.c" \
        "$LWIP/lib/liblwip.a" -lws2_32 -Xlinker /subsystem:console,5.01; then
    if (cd "$OUT" && timeout 60 wine lwip_smoke_neg.exe) >/dev/null 2>&1; then
        bad "negative payload is rejected"
    else
        ok "negative payload is rejected"
    fi
else
    bad "negative payload builds"
fi

printf '\n%d passed, %d failed\n' "$pass" "$fail"
[ "$fail" -eq 0 ] || exit 1
exit 0
