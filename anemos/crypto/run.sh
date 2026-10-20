#!/bin/bash
# P3 groundwork: the cross-built OpenSSL is usable by a 32-bit XP PE.
#
# Checks, each able to fail:
#   1  the pinned clang links kat.c against the i686 OpenSSL static library
#   2  the resulting PE runs under Wine and every published vector matches
#   3  the version string is the pinned 3.5.7 build, not some host library
#   4  every import exists on XP (Wine is not XP; this is the static check)
#   5  a build with a deliberately wrong expected digest FAILS
#
# usage: anemos/crypto/run.sh
set -uo pipefail

HERE=$(cd "$(dirname "$0")" && pwd)
OUT="$HERE/out"
LLVM="$HOME/.local/opt/llvm-mingw-20261006-msvcrt-ubuntu-22.04-x86_64/bin"
OSSL="$HOME/.local/opt/openssl-3.5.7-i686-xp"

export PATH="$LLVM:$PATH"
export WINEDEBUG=-all WINEPREFIX=${WINEPREFIX:-$HOME/.wine-xp}

pass=0
fail=0
ok()  { pass=$((pass + 1)); printf 'PASS  %s\n' "$1"; }
bad() { fail=$((fail + 1)); printf 'FAIL  %s\n' "$1"; }
step() { printf '\n== %s ==\n' "$1"; }

[ -d "$WINEPREFIX" ] || wineboot -i >/dev/null 2>&1
mkdir -p "$OUT"

step "0: the cross-built OpenSSL is installed"
if [ -f "$OSSL/lib/libcrypto.a" ] && [ -f "$OSSL/include/openssl/evp.h" ]; then
    ok "libcrypto.a and headers at $OSSL"
else
    bad "libcrypto.a and headers at $OSSL (run anemos/crypto/build-openssl.sh)"
    printf '\n%d passed, %d failed\n' "$pass" "$fail"
    exit 1
fi

step "1: link the KAT binary for i686 Windows"
if i686-w64-mingw32-clang -O2 -Wall -Wextra -Werror \
        -I"$OSSL/include" -o "$OUT/kat.exe" "$HERE/kat.c" \
        -L"$OSSL/lib" -lcrypto -lws2_32 -lgdi32 -lcrypt32; then
    ok "clang links kat.exe against libcrypto.a"
else
    bad "clang links kat.exe against libcrypto.a"
fi

step "2: known-answer vectors under Wine"
if [ -f "$OUT/kat.exe" ]; then
    if wine "$OUT/kat.exe" 2>&1 | tr -d '\r' | tee "$OUT/kat.log"; then
        ok "every known-answer test passes"
    else
        bad "every known-answer test passes"
    fi
    if grep -q '^OpenSSL: OpenSSL 3\.5\.7 ' "$OUT/kat.log"; then
        ok "binary is linked to the pinned OpenSSL 3.5.7"
    else
        bad "binary is linked to the pinned OpenSSL 3.5.7"
    fi
    if python3 "$HERE/../csp/check_xp_imports.py" "$OUT/kat.exe" \
            --objdump "$LLVM/llvm-objdump" >"$OUT/xp_imports.log" 2>&1; then
        ok "kat.exe imports only APIs present on XP"
    else
        bad "kat.exe imports only APIs present on XP"
        sed 's/^/    /' "$OUT/xp_imports.log"
    fi
else
    bad "known-answer vectors under Wine (no binary)"
fi

step "3: negative control — a wrong expected digest must fail"
if i686-w64-mingw32-clang -O2 -Wall -Wextra -Werror -DANEM_CRYPTO_NEGATIVE \
        -I"$OSSL/include" -o "$OUT/katneg.exe" "$HERE/kat.c" \
        -L"$OSSL/lib" -lcrypto -lws2_32 -lgdi32 -lcrypt32; then
    ok "negative build compiles"
    if wine "$OUT/katneg.exe" >/dev/null 2>&1; then
        bad "negative build is rejected"
    else
        ok "negative build is rejected"
    fi
else
    bad "negative build compiles"
fi

printf '\n%d passed, %d failed\n' "$pass" "$fail"
[ "$fail" -eq 0 ] || exit 1
exit 0
