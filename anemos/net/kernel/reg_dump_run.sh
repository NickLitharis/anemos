#!/usr/bin/env bash
# Dump the guest's network component registry (read-only).
set -uo pipefail
HERE=$(cd "$(dirname "$0")" && pwd)
REPO=$(cd "$HERE/../../.." && pwd)
OUT="$HERE/out/reg"; DISK="${XP_SUBJECT_DISK:-$HOME/vms/xpvmware/xp.vmdk}"
NTFS="$REPO/tools/ntfspatch.py"; RUNNER="$REPO/harness/conformance/runner.py"
VICTIM='\windows\system32\mspaint.exe'
LLVM="$HOME/.local/opt/llvm-mingw-20261006-msvcrt-ubuntu-22.04-x86_64/bin"
die() { printf 'ERROR: %s\n' "$*" >&2; exit 1; }
mkdir -p "$OUT"; export PATH="$LLVM:$PATH"
i686-w64-mingw32-clang -std=c23 -Oz -Wall -Wextra -Werror \
    -o "$HERE/out/reg_dump.exe" "$HERE/reg_dump.c" -ladvapi32 \
    -Xlinker /subsystem:console,5.01 || die build
BAK="$OUT/victim.bin"
restore() { [ -f "$BAK" ] && { python3 "$NTFS" "$DISK" put "$VICTIM" "$BAK" || die restore; rm -f "$BAK"; }; }
[ -z "$(vmrun -T ws list | tail -n +2)" ] || die "VM running"
python3 "$NTFS" "$DISK" get "$VICTIM" "$BAK" || die read
truncate -s "$(stat -c%s "$HERE/out/reg_dump.exe")" "$BAK"
trap restore EXIT
python3 "$NTFS" "$DISK" put "$VICTIM" "$HERE/out/reg_dump.exe" || die install
TAG="reg-$(date +%Y%m%d-%H%M%S)"
python3 "$RUNNER" extract --disk "$DISK" --tag "$TAG" \
    --exe "mspaint.exe \\conftest\\reg.txt" --collect reg.txt \
    --settle 60 --shutdown-grace 300 || die runner
G="$REPO/harness/conformance/out/$TAG/guest-reg.txt"; [ -f "$G" ] || die nocert
restore; trap - EXIT
tr -d '\r' <"$G" > "$OUT/reg.txt"
wc -l "$OUT/reg.txt"
