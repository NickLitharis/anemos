#!/bin/bash
# P3.1: CSP SPI skeleton — exports, contexts, hashing, randomness, properties.
#
# Checks, each able to fail:
#   1  the pinned clang builds anemosp.dll against the cross-built OpenSSL
#   2  all 23 required + 2 optional SPI names are exported undecorated
#   3  every import exists on XP (Wine is not XP; this is the static check)
#   4  a Vista-only import is rejected by that check
#   5  the LoadLibrary harness passes every check under Wine
#   6  a harness built with a wrong expected digest FAILS
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
OSSL="$HOME/.local/opt/openssl-3.5.7-i686-xp"

export PATH="$LLVM:$PATH"
export WINEDEBUG=-all WINEPREFIX=${WINEPREFIX:-$HOME/.wine-xp}

# XP's loader rejects a PE whose subsystem/OS version is 6.0 (modern mingw
# defaults) with "not a valid Win32 application", even when every import is
# present. Guest artifacts must declare XP-era versions.
GUEST_LINK=(-Wl,--major-subsystem-version,5 -Wl,--minor-subsystem-version,1
            -Wl,--major-os-version,5 -Wl,--minor-os-version,1)

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
        "${GUEST_LINK[@]}" -Wl,--kill-at; then
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

step "3: XP load surface"
imports_log="$OUT/xp_imports.log"
: >"$imports_log"
imports_ok=1
for pe in anemosp.dll; do
    if [ -f "$OUT/$pe" ]; then
        if python3 "$HERE/check_xp_imports.py" "$OUT/$pe" \
                --objdump "$LLVM/llvm-objdump" >>"$imports_log" 2>&1; then
            ok "$pe imports only APIs present on XP"
        else
            bad "$pe imports only APIs present on XP"
            imports_ok=0
        fi
    else
        bad "$pe imports only APIs present on XP (no $pe)"
        imports_ok=0
    fi
done
if [ "$imports_ok" -eq 0 ]; then
    sed 's/^/    /' "$imports_log"
fi

step "3b: XP-era PE version"
version_ok=1
for pe in anemosp.dll spi_test.exe; do
    [ -f "$OUT/$pe" ] || continue
    major=$(llvm-readobj --file-headers "$OUT/$pe" \
        | awk '/MajorSubsystemVersion:/{print $2; exit}')
    osver=$(llvm-readobj --file-headers "$OUT/$pe" \
        | awk '/MajorOperatingSystemVersion:/{print $2; exit}')
    if [ "$major" = "5" ] && [ "$osver" = "5" ]; then
        ok "$pe declares an XP-era subsystem/OS version"
    else
        bad "$pe declares an XP-era subsystem/OS version ($major/$osver)"
        version_ok=0
    fi
done

if llvm-objdump -p "$OUT/anemosp.dll" 2>/dev/null | grep -q "SystemFunction036"; then
    ok "the provider seeds from SystemFunction036"
else
    bad "the provider seeds from SystemFunction036"
fi
if llvm-objdump -p "$OUT/anemosp.dll" 2>/dev/null | grep -qE "Crypt(AcquireContext|GenRandom|CreateHash|SignHash|EnumProviders)"; then
    bad "the provider imports no CryptoAPI entry points (re-entry)"
else
    ok "the provider imports no CryptoAPI entry points (re-entry)"
fi

step "4: negative control — a Vista import must be rejected"
if i686-w64-mingw32-clang -O2 -Wall -Wextra -Werror \
        -o "$OUT/bad_vista.exe" "$HERE/bad_vista.c"; then
    ok "Vista-import control builds"
    if python3 "$HERE/check_xp_imports.py" "$OUT/bad_vista.exe" \
            --objdump "$LLVM/llvm-objdump" >/dev/null 2>&1; then
        bad "Vista import is rejected"
    else
        ok "Vista import is rejected"
    fi
else
    bad "Vista-import control builds"
fi

step "5: SPI harness under Wine"
if i686-w64-mingw32-clang -O2 -Wall -Wextra -Werror -I"$HERE" \
        -o "$OUT/spi_test.exe" "$HERE/spi_test.c" \
        -Xlinker /subsystem:console,5.01; then
    ok "harness builds"
    if python3 "$HERE/check_xp_imports.py" "$OUT/spi_test.exe" \
            --objdump "$LLVM/llvm-objdump" >"$OUT/harness_imports.log" 2>&1; then
        ok "spi_test.exe imports only APIs present on XP"
    else
        bad "spi_test.exe imports only APIs present on XP"
        sed 's/^/    /' "$OUT/harness_imports.log"
    fi
    if (cd "$OUT" && wine spi_test.exe anemosp.dll) 2>&1 | tr -d '\r' \
            | tee "$OUT/spi.log"; then
        ok "every SPI check passes"
    else
        bad "every SPI check passes"
    fi
else
    bad "harness builds"
fi

step "6: negative control — a wrong expected digest must fail"
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

step "7: artifact size (report only)"
if [ -f "$OUT/anemosp.dll" ]; then
    size=$(stat -c%s "$OUT/anemosp.dll")
    printf 'anemosp.dll: %s bytes (statically linked OpenSSL)\n' "$size"
fi

printf '\n%d passed, %d failed\n' "$pass" "$fail"
[ "$fail" -eq 0 ] || exit 1
exit 0
