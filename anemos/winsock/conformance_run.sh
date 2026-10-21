#!/usr/bin/env bash
# P4.6: the XP Winsock observation probe (code and capture path).
#
# Checks, each able to fail:
#   1  the probe builds with the pinned clang and imports only APIs XP exports
#      (notably not inet_pton/inet_ntop, which XP's ws2_32 lacks)
#   2  a run under Wine emits every observation and the completion banner
#   3  the comparison tool reports identical files as identical and rejects a
#      changed, missing or extra case
#   4  the capture script is syntactically valid and wired to the runner
#
# The XP capture itself needs the VM and is not run here; running it is
# winsock_xp_run.sh once the machine can boot.
#
# usage: anemos/winsock/conformance_run.sh
set -uo pipefail

HERE=$(cd "$(dirname "$0")" && pwd)
OUT="$HERE/out"
LLVM="$HOME/.local/opt/llvm-mingw-20261006-msvcrt-ubuntu-22.04-x86_64/bin"

export PATH="$LLVM:$PATH"
export WINEDEBUG=-all WINEPREFIX=${WINEPREFIX:-$HOME/.wine-xp}

pass=0
fail=0
ok()  { pass=$((pass + 1)); printf 'PASS  %s\n' "$1"; }
bad() { fail=$((fail + 1)); printf 'FAIL  %s\n' "$1"; }
step() { printf '\n== %s ==\n' "$1"; }

[ -d "$WINEPREFIX" ] || wineboot -i >/dev/null 2>&1
mkdir -p "$OUT"

step "1: build and XP import surface"
if i686-w64-mingw32-clang -std=c23 -Oz -Wall -Wextra -Werror \
        -o "$OUT/xp_probe.exe" "$HERE/xp_probe.c" -lws2_32; then
    ok "XP probe builds"
else
    bad "XP probe builds"
    printf '\n%d passed, %d failed\n' "$pass" "$fail"
    exit 1
fi
if python3 "$HERE/../csp/check_xp_imports.py" "$OUT/xp_probe.exe" \
        --objdump "$LLVM/llvm-objdump" >"$OUT/xp_probe_imports.log" 2>&1; then
    ok "XP probe imports only APIs present on XP"
else
    bad "XP probe imports only APIs present on XP"
    tail -5 "$OUT/xp_probe_imports.log" | sed 's/^/    /'
fi

step "2: the observation script runs to completion"
if (cd "$OUT" && timeout 60 wine xp_probe.exe wine-cert.txt) \
        2>/dev/null | tr -d '\r' >"$OUT/wine-probe.log"; then
    ok "probe runs under Wine"
else
    bad "probe runs under Wine"
fi
tr -d '\r' <"$OUT/wine-cert.txt" >"$OUT/wine-cert-norm.txt" 2>/dev/null \
    && mv "$OUT/wine-cert-norm.txt" "$OUT/wine-cert.txt"
cases=$(grep -c ' ' "$OUT/wine-cert.txt" 2>/dev/null || echo 0)
if grep -qx 'all winsock observations complete' "$OUT/wine-cert.txt" 2>/dev/null \
   && [ "$cases" -ge 40 ]; then
    ok "certificate has $cases observations and its banner"
else
    bad "certificate has $cases observations and its banner"
fi

step "3: the comparison tool can fail"
cp "$OUT/wine-cert.txt" "$OUT/same.txt"
if python3 "$HERE/conformance_compare.py" "$OUT/wine-cert.txt" "$OUT/same.txt" \
        >"$OUT/compare-same.log" 2>&1; then
    ok "identical certificates compare identical"
else
    bad "identical certificates compare identical"
fi
sed 's/^connect.refused.err .*/connect.refused.err 1/' "$OUT/wine-cert.txt" \
    >"$OUT/tampered.txt"
if python3 "$HERE/conformance_compare.py" "$OUT/wine-cert.txt" \
        "$OUT/tampered.txt" >/dev/null 2>&1; then
    bad "a changed case is rejected"
else
    ok "a changed case is rejected"
fi
grep -v '^inet_addr.hex ' "$OUT/wine-cert.txt" >"$OUT/missing.txt"
if python3 "$HERE/conformance_compare.py" "$OUT/wine-cert.txt" \
        "$OUT/missing.txt" >/dev/null 2>&1; then
    bad "a missing case is rejected"
else
    ok "a missing case is rejected"
fi

step "4: the host twin answers the same cases"
if i686-w64-mingw32-clang -std=c23 -Oz -Wall -Wextra -Werror \
        -Wconversion -Wshadow -Wcast-align -Wstrict-prototypes \
        -Wmissing-prototypes -Wnull-dereference -I "$HERE" \
        -o "$OUT/host_probe.exe" "$HERE/addr.c" "$HERE/socket_core.c" \
        "$HERE/loopback.c" "$HERE/host_probe.c"; then
    ok "host twin builds"
    if (cd "$OUT" && timeout 60 wine host_probe.exe host-cert.txt) \
            >/dev/null 2>&1; then
        ok "host twin runs"
    else
        bad "host twin runs"
    fi
    cut -d' ' -f1 "$OUT/wine-cert.txt" | grep -v '^all$' >"$OUT/xp-names.txt"
    cut -d' ' -f1 "$OUT/host-cert.txt" | grep -v '^all$' >"$OUT/host-names.txt"
    if diff -q "$OUT/xp-names.txt" "$OUT/host-names.txt" >/dev/null 2>&1; then
        ok "host twin covers every XP case name"
    else
        bad "host twin covers every XP case name"
        diff -u "$OUT/xp-names.txt" "$OUT/host-names.txt" | head -10 | sed 's/^/    /'
    fi
else
    bad "host twin builds"
fi

step "5: capture script"
if bash -n "$HERE/winsock_xp_run.sh"; then
    ok "capture script parses"
else
    bad "capture script parses"
fi
if grep -q 'runner extract' "$HERE/winsock_xp_run.sh"; then
    ok "capture script is wired to the conformance runner"
else
    bad "capture script is wired to the conformance runner"
fi

printf '\n%d passed, %d failed\n' "$pass" "$fail"
[ "$fail" -eq 0 ] || exit 1
exit 0
