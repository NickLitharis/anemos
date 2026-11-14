#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
OUT="$ROOT/out/ak3"
LLVM_BIN="${LLVM_BIN:-$HOME/.local/opt/llvm-mingw-20261006-msvcrt-ubuntu-22.04-x86_64/bin}"
CC="${CC:-$LLVM_BIN/clang}"
LD="${LD:-$LLVM_BIN/ld.lld}"
QEMU="${QEMU:-qemu-system-x86_64}"
LIMINE_VERSION=12.9.6
LIMINE_SHA256=b38aabb042b0c0f9f562b7741be3c1204838d7bbacfa78bb411c674bd5b984ee
CACHE_ROOT="${XDG_CACHE_HOME:-$HOME/.cache}/anemos"
LIMINE_DIR="$CACHE_ROOT/limine-$LIMINE_VERSION/limine-binary"
ISO_ROOT="$OUT/iso"
ISO="$OUT/anemos.iso"
SERIAL_LOG="$OUT/serial.log"
QEMU_LOG="$OUT/qemu.log"

for tool in "$CC" "$LD" genisoimage make curl sha256sum tar timeout "$QEMU"; do
    command -v "$tool" >/dev/null 2>&1 || {
        printf 'Missing required tool: %s\n' "$tool" >&2
        exit 1
    }
done

if ! "$CC" --version | head -n 1 | grep -Fq '23.1.3'; then
    printf 'AK3 requires pinned clang 23.1.3: %s\n' "$CC" >&2
    exit 1
fi
if ! "$LD" --version | head -n 1 | grep -Fq '23.1.3'; then
    printf 'AK3 requires pinned ld.lld 23.1.3: %s\n' "$LD" >&2
    exit 1
fi

if [[ ! -x "$LIMINE_DIR/limine" ]]; then
    mkdir -p "$CACHE_ROOT"
    TEMP_DIR="$(mktemp -d "$CACHE_ROOT/limine-download.XXXXXX")"
    trap 'rm -rf "$TEMP_DIR"' EXIT
    curl -fsSL "https://github.com/Limine-Bootloader/Limine/releases/download/v$LIMINE_VERSION/limine-binary.tar.gz" \
        -o "$TEMP_DIR/limine-binary.tar.gz"
    printf '%s  %s\n' "$LIMINE_SHA256" "$TEMP_DIR/limine-binary.tar.gz" | sha256sum -c -
    tar -xzf "$TEMP_DIR/limine-binary.tar.gz" -C "$TEMP_DIR"
    make -C "$TEMP_DIR/limine-binary"
    mkdir -p "$(dirname -- "$LIMINE_DIR")"
    mv "$TEMP_DIR/limine-binary" "$LIMINE_DIR"
    rm -f "$TEMP_DIR/limine-binary.tar.gz"
    rmdir "$TEMP_DIR"
    trap - EXIT
fi

mkdir -p "$OUT" "$ISO_ROOT/boot"
for source in main serial interrupts limine_requests memory paging heap memory_test; do
    "$CC" \
        -std=c23 \
        -target x86_64-unknown-elf \
        -Oz -Wall -Wextra -Werror \
        -ffreestanding -fno-builtin -fno-stack-protector \
        -fno-pic -fno-pie -mno-red-zone -mcmodel=kernel \
        -mno-mmx -mno-sse -mno-sse2 \
        -c "$ROOT/kernel/$source.c" -o "$OUT/$source.o"
done
"$CC" \
    -target x86_64-unknown-elf \
    -ffreestanding -fno-pic -fno-pie -mno-red-zone \
    -c "$ROOT/kernel/interrupts.S" -o "$OUT/interrupts-asm.o"
"$LD" \
    -nostdlib -static --no-undefined -z max-page-size=0x1000 \
    -T "$ROOT/kernel/linker.ld" -o "$OUT/anemos.elf" \
    "$OUT/main.o" "$OUT/serial.o" "$OUT/interrupts.o" \
    "$OUT/limine_requests.o" "$OUT/memory.o" "$OUT/paging.o" \
    "$OUT/heap.o" "$OUT/memory_test.o" "$OUT/interrupts-asm.o"

cp "$OUT/anemos.elf" "$ISO_ROOT/boot/anemos.elf"
cp "$ROOT/boot/limine.conf" "$ISO_ROOT/limine.conf"
cp "$LIMINE_DIR/limine-bios.sys" "$ISO_ROOT/limine-bios.sys"
cp "$LIMINE_DIR/limine-bios-cd.bin" "$ISO_ROOT/boot/limine-bios-cd.bin"
genisoimage -R -r -J \
    -b boot/limine-bios-cd.bin -no-emul-boot -boot-load-size 4 -boot-info-table \
    -o "$ISO" "$ISO_ROOT" >/dev/null
"$LIMINE_DIR/limine" bios-install --force "$ISO"

rm -f "$SERIAL_LOG" "$QEMU_LOG"
set +e
timeout 10s "$QEMU" \
    -machine q35 -m 256M -cdrom "$ISO" \
    -serial "file:$SERIAL_LOG" -monitor none -display none \
    -no-reboot -no-shutdown >"$QEMU_LOG" 2>&1
QEMU_STATUS=$?
set -e
if [[ "$QEMU_STATUS" -ne 124 ]]; then
    printf 'QEMU exited unexpectedly (status %s); see %s\n' "$QEMU_STATUS" "$QEMU_LOG" >&2
    exit 1
fi
for expected in 'ANEMOS: boot ok' 'AK3: memory tests ok' 'AK2: timer ok' 'PANIC: exception vector=0'; do
    if ! tr -d '\r' < "$SERIAL_LOG" | grep -Fq "$expected"; then
        printf 'Missing serial output "%s"; see %s and %s\n' "$expected" "$SERIAL_LOG" "$QEMU_LOG" >&2
        exit 1
    fi
done
printf 'AK3 passed: %s\n' "$SERIAL_LOG"
