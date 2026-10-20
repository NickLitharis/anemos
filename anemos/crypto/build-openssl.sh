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
# usage: anemos/crypto/build-openssl.sh [--force]
set -euo pipefail

VERSION=3.5.7
LLVM="$HOME/.local/opt/llvm-mingw-20261006-msvcrt-ubuntu-22.04-x86_64/bin"
SRC="$HOME/.local/src"
PREFIX="$HOME/.local/opt/openssl-$VERSION-i686"
TARBALL="openssl-$VERSION.tar.gz"
URL="https://github.com/openssl/openssl/releases/download/openssl-$VERSION/$TARBALL"

if [ -f "$PREFIX/lib/libcrypto.a" ] && [ -f "$PREFIX/include/openssl/evp.h" ] \
   && [ "${1:-}" != "--force" ]; then
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

export PATH="$LLVM:$PATH"
CC=clang ./Configure mingw \
    --cross-compile-prefix=i686-w64-mingw32- \
    --prefix="$PREFIX" \
    no-shared no-tests no-asm

make -j"$(nproc)"
make install_sw

mkdir -p "$PREFIX"
cat > "$PREFIX/BUILD.txt" <<EOF
OpenSSL $VERSION, i686-w64-mingw32, static, built $(date -u +%Y-%m-%dT%H:%M:%SZ)
CC=clang (llvm-mingw 20261006, clang 23.1.3)
Configure: mingw --cross-compile-prefix=i686-w64-mingw32- --prefix=$PREFIX no-shared no-tests no-asm
EOF
echo "installed $PREFIX"
