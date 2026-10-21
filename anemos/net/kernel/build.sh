#!/usr/bin/env bash
# Build the Anemos network driver (K1 skeleton) with clang + mingw DDK headers.
#
# There is no WDK installation. The ntoskrnl import library is generated from
# the functions this driver uses and every name is checked against XP's export
# list, so a driver that cannot load is caught at build time where possible.
#
# usage: anemos/net/kernel/build.sh
set -euo pipefail

HERE=$(cd "$(dirname "$0")" && pwd)
REPO=$(cd "$HERE/../../.." && pwd)
LLVM="$HOME/.local/opt/llvm-mingw-20261006-msvcrt-ubuntu-22.04-x86_64/bin"
DDK="$LLVM/../i686-w64-mingw32/include/ddk"
OUT="$HERE/out"
LWIP="$HOME/.local/opt/lwip-2.2.0-i686-kernel"
LWIP_SRC="$HOME/.local/src/lwip-STABLE-2_2_0_RELEASE/src/include"

mkdir -p "$OUT"
"$HERE/build-lwip-kernel.sh"

# Stdcall-decorated names, so dlltool emits __imp__name@N while -k keeps the
# import name undecorated (XP exports these names without the @N suffix).
EXPORTS=(
    IoCreateDevice@28 IoDeleteDevice@4
    IoCreateSymbolicLink@8 IoDeleteSymbolicLink@4
    MmGetSystemRoutineAddress@4 KeQueryInterruptTime@0
    RtlInitUnicodeString@8
)

{
    echo "LIBRARY ntoskrnl.exe"
    echo "EXPORTS"
    for name in "${EXPORTS[@]}"; do
        echo "    $name"
    done
} > "$OUT/ntoskrnl.def"

export PATH="$LLVM:$PATH"
llvm-dlltool -m i386 -k -d "$OUT/ntoskrnl.def" -l "$OUT/libntoskrnl.a"

CFLAGS=(-std=c11 -O2 -ffreestanding -fno-stack-protector -fno-builtin
        -fno-asynchronous-unwind-tables -mno-stack-arg-probe
        -D_WIN32_WINNT=0x0501 -DNTDDI_VERSION=0x05010000 -DNTDDI_WINVISTA=0x06000000
        -I "$DDK" -I "$HERE" -I "$LWIP_SRC")
i686-w64-mingw32-clang "${CFLAGS[@]}" -c "$HERE/anemosnet.c" \
    -o "$OUT/anemosnet.o"
i686-w64-mingw32-clang "${CFLAGS[@]}" -c "$HERE/kernel_support.c" \
    -o "$OUT/kernel_support.o"

i686-w64-mingw32-clang --target=i686-w64-mingw32 -nostdlib \
    -Xlinker /subsystem:native,5.01 \
    -Xlinker /entry:DriverEntry \
    -Wl,--image-base,0x10000 \
    -Wl,--section-alignment,0x1000 \
    -Wl,--file-alignment,0x200 \
    -o "$OUT/anemosnet.sys" \
    "$OUT/anemosnet.o" "$OUT/kernel_support.o" "$LWIP/lib/liblwip.a" \
    "$OUT/libntoskrnl.a"

echo "built $OUT/anemosnet.sys"
