#!/usr/bin/env bash
# P3.5: drive the provider through CryptoAPI (advapi32), not the SPI.
#
# Stock XP verifies the provider image signature before CPAcquireContext, and
# an unsigned DLL cannot pass that (docs/P3-CRYPTO.md). Wine does not enforce
# the check, so this gate runs on the host and proves *plumbing* only:
# registration, CryptoAPI's parameter translation, handle mapping and blob
# conventions all agree with the provider. It is deliberately not called an XP
# result; the XP signature question stays open.
#
# The provider is registered in the throwaway Wine prefix's 32-bit registry
# view (the harness is a 32-bit PE), pointing at the DLL this run built, and
# the registration is removed afterwards.
#
# usage: anemos/csp/api_run.sh
set -uo pipefail

HERE=$(cd "$(dirname "$0")" && pwd)
OUT="$HERE/out"
LLVM="$HOME/.local/opt/llvm-mingw-20261006-msvcrt-ubuntu-22.04-x86_64/bin"
PROVIDER="Anemos OpenSSL CSP"
REGPATH="HKLM\\Software\\Microsoft\\Cryptography\\Defaults\\Provider\\$PROVIDER"

export PATH="$LLVM:$PATH"
export WINEDEBUG=-all WINEPREFIX=${WINEPREFIX:-$HOME/.wine-xp}

pass=0
fail=0
ok()  { pass=$((pass + 1)); printf 'PASS  %s\n' "$1"; }
bad() { fail=$((fail + 1)); printf 'FAIL  %s\n' "$1"; }
step() { printf '\n== %s ==\n' "$1"; }
die() { printf '\nERROR: %s\n' "$*" >&2; exit 1; }

[ -d "$WINEPREFIX" ] || wineboot -i >/dev/null 2>&1
mkdir -p "$OUT"

cleanup() {
    wine reg delete "$REGPATH" /f /reg:32 >/dev/null 2>&1 || true
}
trap cleanup EXIT

step "1: host gate and artifacts"
if "$HERE/run.sh" >"$OUT/api-host-run.log" 2>&1; then
    ok "host SPI gate is green"
else
    die "host SPI gate failed; see $OUT/api-host-run.log"
fi
[ -f "$OUT/anemosp.dll" ] || die "no provider DLL at $OUT/anemosp.dll"

step "2: build the CryptoAPI harness"
if i686-w64-mingw32-clang -O2 -Wall -Wextra -Werror \
        -o "$OUT/cryptapi_test.exe" "$HERE/cryptapi_test.c" -ladvapi32; then
    ok "cryptapi_test.exe builds"
else
    die "cryptapi_test.exe build failed"
fi
if python3 "$HERE/check_xp_imports.py" "$OUT/cryptapi_test.exe" \
        --objdump "$LLVM/llvm-objdump" >"$OUT/cryptapi_imports.log" 2>&1; then
    ok "cryptapi_test.exe imports only APIs present on XP"
else
    bad "cryptapi_test.exe imports only APIs present on XP"
    sed 's/^/    /' "$OUT/cryptapi_imports.log"
fi

step "3: register the provider with CryptoAPI"
DLL_WIN=$(winepath -w "$OUT/anemosp.dll" 2>/dev/null | tr -d '\r')
[ -n "$DLL_WIN" ] || die "winepath could not map the DLL"
if wine reg add "$REGPATH" /v 'Image Path' /t REG_SZ /d "$DLL_WIN" \
        /f /reg:32 >/dev/null 2>&1 \
   && wine reg add "$REGPATH" /v Type /t REG_DWORD /d 1 \
        /f /reg:32 >/dev/null 2>&1; then
    ok "provider registered (32-bit view): $DLL_WIN"
else
    die "could not register the provider in $WINEPREFIX"
fi

step "4: CryptoAPI path"
if (cd "$OUT" && wine cryptapi_test.exe) 2>&1 | tr -d '\r' | tee "$OUT/cryptapi.log"; then
    ok "every CryptoAPI-path check passes"
else
    bad "every CryptoAPI-path check passes"
fi

step "5: negative control — a wrong expected digest must fail"
if i686-w64-mingw32-clang -O2 -Wall -Wextra -Werror -DANEM_CSP_NEGATIVE \
        -o "$OUT/cryptapi_test_neg.exe" "$HERE/cryptapi_test.c" -ladvapi32; then
    ok "negative harness builds"
    if (cd "$OUT" && wine cryptapi_test_neg.exe) >/dev/null 2>&1; then
        bad "negative harness is rejected"
    else
        ok "negative harness is rejected"
    fi
else
    bad "negative harness builds"
fi

printf '\n%d passed, %d failed\n' "$pass" "$fail"
[ "$fail" -eq 0 ] || exit 1
exit 0
