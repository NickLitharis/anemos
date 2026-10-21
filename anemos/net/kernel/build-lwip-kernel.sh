#!/usr/bin/env bash
# Build the pinned lwIP for the kernel driver: freestanding, NO_SYS, no CRT.
# usage: anemos/net/kernel/build-lwip-kernel.sh [--force]
set -euo pipefail
VERSION=2.2.0
TAG=STABLE-2_2_0_RELEASE
LLVM="$HOME/.local/opt/llvm-mingw-20261006-msvcrt-ubuntu-22.04-x86_64/bin"
SRC="$HOME/.local/src/lwip-$TAG"
PREFIX="$HOME/.local/opt/lwip-$VERSION-i686-kernel"
HERE=$(cd "$(dirname "$0")" && pwd)

if [ -f "$PREFIX/lib/liblwip.a" ] && [ "${1:-}" != "--force" ]; then
    echo "lwIP kernel library already built at $PREFIX"
    exit 0
fi
[ -d "$SRC" ] || { echo "missing lwIP source at $SRC"; exit 1; }
mkdir -p "$PREFIX/lib" "$PREFIX/obj"
export PATH="$LLVM:$PATH"
CFLAGS=(-std=c11 -O2 -w -ffreestanding -fno-builtin -fno-stack-protector
        -fno-asynchronous-unwind-tables -mno-stack-arg-probe
        "-I$SRC/src/include" "-I$HERE")
SOURCES=(
    core/def.c core/inet_chksum.c core/init.c core/ip.c core/mem.c
    core/memp.c core/netif.c core/pbuf.c core/stats.c core/sys.c
    core/tcp.c core/tcp_in.c core/tcp_out.c core/timeouts.c core/udp.c
    core/ipv4/icmp.c core/ipv4/ip4.c core/ipv4/ip4_addr.c
    core/ipv4/ip4_frag.c
)
OBJ=()
for src in "${SOURCES[@]}"; do
    obj="$PREFIX/obj/$(echo "$src" | tr '/' '_').o"
    i686-w64-mingw32-clang "${CFLAGS[@]}" -c "$SRC/src/$src" -o "$obj"
    OBJ+=("$obj")
done
llvm-ar rcs "$PREFIX/lib/liblwip.a" "${OBJ[@]}"
echo "installed $PREFIX/lib/liblwip.a"
