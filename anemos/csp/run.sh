#!/bin/bash
# P3.1: CSP SPI skeleton — exports, contexts, hashing, randomness, properties.
#
# Checks, each able to fail:
#   1  the pinned clang builds anemosp.dll against the cross-built OpenSSL
#   2  all 23 required + 2 optional SPI names are exported undecorated
#   3  the LoadLibrary harness passes every check under Wine
#   4  a harness built with a wrong expected digest FAILS
#
# The harness calls the SPI directly. It deliberately does not go through
# CryptAcquireContext: stock XP verifies the provider image signature first
# (docs/P3-CRYPTO.md), which an unsigned DLL cannot pass.
#
# usage: anemos/csp/run.sh
set -uo pipefail

HERE=$(cd "$(dirname "$0")" && pwd)
OUT="$HERE/out"
LLVM="$HOME/.local/opt/llvm-mingw-20261006-msvcrt-ubuntu-22.04-x86_64/bin"
OSSL="$HOME/.local/opt/openssl-3.5.7-i686"

export PATH="$LLVM:$PATH"
export WINEDEBUG=-all WINEPREFIX=${WINEPREFIX:-$HOME/.wine-xp}

pass=0
fail=0
ok()  { pass=$((pass + 1)); printf 'PASS  %s\n' "$1"; }
bad() { fail=$((fail + 1)); printf 'FAIL  %s\n' "$1"; }
step() { printf '\n== %s ==\n' "$1"; }

[ -d "$WINEPREFIX" ] || wineboot -i >/dev/null 2>&1
mkdir -p "$OUT"

step "0: dependencies"
if [ -f "$OSSL/lib/libcrypto.a" ] && [ -f "$OSSL/include/openssl/evp.h" ]; then
    ok "cross-built OpenSSL is present"
else
    bad "cross-built OpenSSL is present (run anemos/crypto/build-openssl.sh)"
    printf '\n%d passed, %d failed\n' "$pass" "$fail"
    exit 1
fi

step "1: build the provider DLL"
if i686-w64-mingw32-clang -O2 -Wall -Wextra -Werror -shared \
        -I"$OSSL/include" -I"$HERE" \
        -o "$OUT/anemosp.dll" "$HERE/csp.c" \
        -L"$OSSL/lib" -lcrypto -ladvapi32 -lws2_32 -lgdi32 -lcrypt32 \
        -Wl,--kill-at; then
    ok "clang builds anemosp.dll"
else
    bad "clang builds anemosp.dll"
fi

step "2: the export surface"
if [ -f "$OUT/anemosp.dll" ]; then
    exports=$(llvm-readobj --coff-exports "$OUT/anemosp.dll" 2>/dev/null)
    missing=0
    for name in CPAcquireContext CPReleaseContext CPGenKey CPDeriveKey \
                CPDestroyKey CPSetKeyParam CPGetKeyParam CPExportKey \
                CPImportKey CPEncrypt CPDecrypt CPCreateHash CPHashData \
                CPHashSessionKey CPDestroyHash CPSignHash CPVerifySignature \
                CPGenRandom CPGetUserKey CPSetProvParam CPGetProvParam \
                CPSetHashParam CPGetHashParam CPDuplicateKey CPDuplicateHash; do
        if ! printf '%s\n' "$exports" | grep -q "Name: $name$"; then
            printf '    missing export: %s\n' "$name"
            missing=$((missing + 1))
        fi
    done
    if [ "$missing" -eq 0 ]; then
        ok "all 25 SPI names exported undecorated"
    else
        bad "all 25 SPI names exported undecorated ($missing missing)"
    fi
else
    bad "the export surface (no DLL)"
fi

step "3: SPI harness under Wine"
if i686-w64-mingw32-clang -O2 -Wall -Wextra -Werror -I"$HERE" \
        -o "$OUT/spi_test.exe" "$HERE/spi_test.c"; then
    ok "harness builds"
    if (cd "$OUT" && wine spi_test.exe anemosp.dll) 2>&1 | tr -d '\r' \
            | tee "$OUT/spi.log"; then
        ok "every SPI check passes"
    else
        bad "every SPI check passes"
    fi
else
    bad "harness builds"
fi

step "4: negative control — a wrong expected digest must fail"
if i686-w64-mingw32-clang -O2 -Wall -Wextra -Werror -DANEM_CSP_NEGATIVE \
        -I"$HERE" -o "$OUT/spi_test_neg.exe" "$HERE/spi_test.c"; then
    ok "negative harness builds"
    if (cd "$OUT" && wine spi_test_neg.exe anemosp.dll) >/dev/null 2>&1; then
        bad "negative harness is rejected"
    else
        ok "negative harness is rejected"
    fi
else
    bad "negative harness builds"
fi

step "5: artifact size (report only)"
if [ -f "$OUT/anemosp.dll" ]; then
    size=$(stat -c%s "$OUT/anemosp.dll")
    printf 'anemosp.dll: %s bytes (statically linked OpenSSL)\n' "$size"
fi

printf '\n%d passed, %d failed\n' "$pass" "$fail"
[ "$fail" -eq 0 ] || exit 1
exit 0
