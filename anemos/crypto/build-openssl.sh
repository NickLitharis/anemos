#!/bin/bash
# Cross-build OpenSSL for i686 Windows — the engine behind the P3 CSP.
#
# The pinned llvm-mingw toolchain does not ship OpenSSL, and the CSP must run
# in 32-bit XP, so libcrypto is built here once and linked statically. This is
# deliberately outside Source/ and outside the deliverable's build: OpenSSL is
# a dependency, not Anemos code.
#
# Target name is `mingw` (32-bit), NOT `mingw32` — OpenSSL 3.x renamed it, and
# passing mingw32 makes Configure print the target list and exit 1.
#
# Threading is disabled on purpose. OpenSSL's Windows threading layer imports
# SRW locks and condition variables (Vista+) and its async layer imports fiber
# APIs; the resulting DLL passes every KAT under Wine but cannot load on XP.
# With no-threads/no-async/no-thread-pool the import surface is XP-clean. The
# CSP serialises OpenSSL calls with its own critical section, so it does not
# need the library's internal locks.
#
# usage: anemos/crypto/build-openssl.sh [--force]
set -euo pipefail

VERSION=3.5.7
LLVM="$HOME/.local/opt/llvm-mingw-20261006-msvcrt-ubuntu-22.04-x86_64/bin"
SRC="$HOME/.local/src"
PREFIX="$HOME/.local/opt/openssl-$VERSION-i686-xp"
TARBALL="openssl-$VERSION.tar.gz"
URL="https://github.com/openssl/openssl/releases/download/openssl-$VERSION/$TARBALL"

write_note() {
    mkdir -p "$PREFIX"
    cat > "$PREFIX/BUILD.txt" <<EOF
OpenSSL $VERSION, i686-w64-mingw32, static, built $(date -u +%Y-%m-%dT%H:%M:%SZ)
CC=clang (llvm-mingw 20261006, clang 23.1.3)
Configure: mingw --cross-compile-prefix=i686-w64-mingw32- --prefix=$PREFIX no-shared no-tests no-asm no-threads no-async no-thread-pool no-engine
RAND: rand_win.c patched to seed from SystemFunction036 (RtlGenRandom), not CryptoAPI.
Engines: none, so the CryptoAPI engine (e_capi) cannot re-enter the provider.
Import surface: checked against anemos/csp/xp_exports/ (from xp-pristine.vmdk)
EOF
}

if [ -f "$PREFIX/lib/libcrypto.a" ] && [ -f "$PREFIX/include/openssl/evp.h" ] \
   && [ "${1:-}" != "--force" ]; then
    [ -f "$PREFIX/BUILD.txt" ] || write_note
    echo "OpenSSL $VERSION is already installed at $PREFIX"
    exit 0
fi

mkdir -p "$SRC"
cd "$SRC"
if [ ! -f "$TARBALL" ]; then
    echo "downloading $URL"
    curl -L --fail -o "$TARBALL" "$URL"
fi
rm -rf "openssl-$VERSION"
tar xzf "$TARBALL"
cd "openssl-$VERSION"

# OpenSSL's Windows seed source calls CryptoAPI (CryptAcquireContext /
# CryptGenRandom). When this DLL sits in the rsaenh slot, that call re-enters
# the provider. Replace the entropy source with RtlGenRandom, which is
# independent of CryptoAPI. Pinned to 3.5.7; the build fails if the block no
# longer matches.
python3 - <<'PY'
from pathlib import Path

path = Path("providers/implementations/rands/seeding/rand_win.c")
text = path.read_text()
decl = "#ifndef USE_BCRYPTGENRANDOM\n    HCRYPTPROV hProvider;\n#endif\n"
if decl not in text:
    raise SystemExit("rand_win.c: provider declaration changed")
text = text.replace(decl, "")

start = text.index("#else\n    bytes_needed = ossl_rand_pool_bytes_needed")
end = text.index("#endif\n\n    return ossl_rand_pool_entropy_available(pool);")
replacement = '''#else
    bytes_needed = ossl_rand_pool_bytes_needed(pool, 1 /*entropy_factor*/);
    buffer = ossl_rand_pool_add_begin(pool, bytes_needed);
    if (buffer != NULL) {
        size_t bytes = 0;

        /* RtlGenRandom is independent of CryptoAPI: an OpenSSL RAND call can
         * never re-enter a provider installed as rsaenh.dll. */
        if (SystemFunction036(buffer, (ULONG)bytes_needed))
            bytes = bytes_needed;
        ossl_rand_pool_add_end(pool, bytes, 8 * bytes);
        entropy_available = ossl_rand_pool_entropy_available(pool);
    }
    if (entropy_available > 0)
        return entropy_available;
'''
text = text[:start] + replacement + text[end:]
text = text.replace(
    "#include <windows.h>",
    "#include <windows.h>\n\n"
    "BOOLEAN WINAPI SystemFunction036(PVOID pbBuffer, ULONG cbBuffer);",
    1)
path.write_text(text)
print("rand_win.c patched to RtlGenRandom")
PY

export PATH="$LLVM:$PATH"
CC=clang ./Configure mingw \
    --cross-compile-prefix=i686-w64-mingw32- \
    --prefix="$PREFIX" \
    no-shared no-tests no-asm no-threads no-async no-thread-pool no-engine

make -j"$(nproc)"
make install_sw

write_note
echo "installed $PREFIX"
