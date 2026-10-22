#!/usr/bin/env bash
# Install AnemosNet through netcfgx and check NDIS binding.
set -uo pipefail
HERE=$(cd "$(dirname "$0")" && pwd)
REPO=$(cd "$HERE/../../.." && pwd)
OUT="$HERE/out/netcfg"; DISK="${XP_SUBJECT_DISK:-$HOME/vms/xpvmware/xp.vmdk}"
NTFS="$REPO/tools/ntfspatch.py"; RUNNER="$REPO/harness/conformance/runner.py"
VICTIM='\windows\system32\mspaint.exe'
LLVM="$HOME/.local/opt/llvm-mingw-20261006-msvcrt-ubuntu-22.04-x86_64/bin"
die() { printf 'ERROR: %s\n' "$*" >&2; exit 1; }
mkdir -p "$OUT"; export PATH="$LLVM:$PATH"
"$HERE/m1_build.sh" >"$OUT/build.log" 2>&1 || die "driver build failed"
i686-w64-mingw32-clang -std=c23 -Oz -Wall -Wextra -Werror \
    -I "$HERE/out" -o "$HERE/out/netcfg_install.exe" "$HERE/netcfg_install.c" \
    -lole32 -ladvapi32 -lsetupapi -Xlinker /subsystem:console,5.01 || die "netcfg build failed"
BAK="$OUT/victim.bin"
restore() { [ -f "$BAK" ] && { python3 "$NTFS" "$DISK" put "$VICTIM" "$BAK" || die restore; rm -f "$BAK"; }; }
[ -z "$(vmrun -T ws list | tail -n +2)" ] || die "VM running"
python3 "$NTFS" "$DISK" get "$VICTIM" "$BAK" || die read
truncate -s "$(stat -c%s "$HERE/out/netcfg_install.exe")" "$BAK"
trap restore EXIT
python3 "$NTFS" "$DISK" put "$VICTIM" "$HERE/out/netcfg_install.exe" || die install
TAG="netcfg-$(date +%Y%m%d-%H%M%S)"
python3 "$RUNNER" extract --disk "$DISK" --tag "$TAG" \
    --exe "mspaint.exe \\conftest\\netcfg.txt" --collect netcfg.txt \
    --settle 90 --shutdown-grace 300 || die runner
G="$REPO/harness/conformance/out/$TAG/guest-netcfg.txt"; [ -f "$G" ] || die nocert
restore; trap - EXIT
tr -d '\r' <"$G" > "$OUT/netcfg.txt"
cat "$OUT/netcfg.txt"
