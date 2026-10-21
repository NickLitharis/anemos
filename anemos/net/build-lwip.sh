#!/usr/bin/env bash
# Build lwIP 2.2.0 for i686 Windows, NO_SYS, for the Anemos transport spike.
#
# lwIP is BSD-3-Clause; it lives outside the repository like OpenSSL and is a
# dependency, not Anemos code. The pinned source is the STABLE-2_2_0_RELEASE
# tag, fetched once into ~/.local/src.
#
# usage: anemos/net/build-lwip.sh [--force]
set -euo pipefail

VERSION=2.2.0
TAG=STABLE-2_2_0_RELEASE
LLVM="$HOME/.local/opt/llvm-mingw-20261006-msvcrt-ubuntu-22.04-x86_64/bin"
SRC="$HOME/.local/src"
DIR="$SRC/lwip-$TAG"
PREFIX="$HOME/.local/opt/lwip-$VERSION-i686"
TARBALL="$SRC/lwip-$VERSION.tar.gz"
URL="https://github.com/lwip-tcpip/lwip/archive/refs/tags/$TAG.tar.gz"
REPO=$(cd "$(dirname "$0")/../.." && pwd)

if [ -f "$PREFIX/lib/liblwip.a" ] && [ "${1:-}" != "--force" ]; then
    echo "lwIP $VERSION is already installed at $PREFIX"
    exit 0
fi

mkdir -p "$SRC" "$PREFIX/lib" "$PREFIX/include"
if [ ! -f "$TARBALL" ]; then
    echo "downloading $URL"
    curl -sL --fail -o "$TARBALL" "$URL"
fi
rm -rf "$DIR"
tar xzf "$TARBALL" -C "$SRC"
[ -d "$DIR" ] || { echo "unexpected archive layout"; exit 1; }

export PATH="$LLVM:$PATH"
mkdir -p "$PREFIX/obj"
OBJ=()
CFLAGS=(-std=c11 -O2 -w -I"$DIR/src/include" -I"$REPO/anemos/net")
SOURCES=(
    core/def.c core/inet_chksum.c core/init.c core/ip.c core/mem.c
    core/memp.c core/netif.c core/pbuf.c core/stats.c core/sys.c
    core/tcp.c core/tcp_in.c core/tcp_out.c core/timeouts.c core/udp.c
    core/ipv4/icmp.c core/ipv4/ip4.c core/ipv4/ip4_addr.c
    core/ipv4/ip4_frag.c
)
for src in "${SOURCES[@]}"; do
    obj="$PREFIX/obj/$(echo "$src" | tr '/' '_').o"
    i686-w64-mingw32-clang "${CFLAGS[@]}" -c "$DIR/src/$src" -o "$obj"
    OBJ+=("$obj")
done
llvm-ar rcs "$PREFIX/lib/liblwip.a" "${OBJ[@]}"
rm -rf "$PREFIX/include/lwip"
cp -r "$DIR/src/include/lwip" "$PREFIX/include/"
cp "$REPO/anemos/net/lwipopts.h" "$PREFIX/include/lwipopts.h"

{
    echo "lwIP $VERSION ($TAG), i686-w64-mingw32, NO_SYS, built $(date -u +%Y-%m-%dT%H:%M:%SZ)"
    echo "Config: anemos/net/lwipopts.h (IPv4, TCP, UDP, loopif; no sockets API)"
    echo "Sources: ${SOURCES[*]}"
} > "$PREFIX/BUILD.txt"
echo "installed $PREFIX"
