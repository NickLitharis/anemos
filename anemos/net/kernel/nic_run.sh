#!/usr/bin/env bash
# Install XP's own e1000 driver in the guest (K3 prerequisite).
# usage: nic_run.sh
set -uo pipefail
HERE=$(cd "$(dirname "$0")" && pwd)
REPO=$(cd "$HERE/../../.." && pwd)
OUT="$HERE/out/nic"
DISK="${XP_SUBJECT_DISK:-$HOME/vms/xpvmware/xp.vmdk}"
NTFS="$REPO/tools/ntfspatch.py"
RUNNER="$REPO/harness/conformance/runner.py"
VICTIM='\windows\system32\mspaint.exe'
LLVM="$HOME/.local/opt/llvm-mingw-20261006-msvcrt-ubuntu-22.04-x86_64/bin"
die() { printf 'ERROR: %s\n' "$*" >&2; exit 1; }
say() { printf '%s\n' "$*"; }
mkdir -p "$OUT"
export PATH="$LLVM:$PATH"
i686-w64-mingw32-clang -std=c23 -Oz -Wall -Wextra -Werror \
    -o "$HERE/out/nic_install.exe" "$HERE/nic_install.c" \
    -lsetupapi -lnewdev -liphlpapi -ladvapi32 \
    -Xlinker /subsystem:console,5.01 || die "build failed"
SIZE=$(stat -c%s "$HERE/out/nic_install.exe")
BAK="$OUT/victim-mspaint.exe"
restore() {
    if [ -f "$BAK" ]; then
        python3 "$NTFS" "$DISK" put "$VICTIM" "$BAK" || die "restore FAILED"
        rm -f "$BAK"; say "restored $VICTIM"
    fi
}
[ -z "$(vmrun -T ws list | tail -n +2)" ] || die "VM is running"
python3 "$NTFS" "$DISK" get "$VICTIM" "$BAK" || die "cannot read victim"
truncate -s "$SIZE" "$BAK"
trap restore EXIT
python3 "$NTFS" "$DISK" put "$VICTIM" "$HERE/out/nic_install.exe" || die "install failed"
TAG="nic-$(date +%Y%m%d-%H%M%S)"
say "== boot and install the in-box e1000 driver (tag $TAG) =="
python3 "$RUNNER" extract --disk "$DISK" --tag "$TAG" \
    --exe "mspaint.exe \\conftest\\nic.txt" --collect nic.txt \
    --settle 120 --shutdown-grace 300 || die "runner failed"
GUEST="$REPO/harness/conformance/out/$TAG/guest-nic.txt"
[ -f "$GUEST" ] || die "no certificate"
restore; trap - EXIT
cp "$GUEST" "$OUT/nic.txt"; tr -d '\r' <"$OUT/nic.txt" >"$OUT/t"; mv "$OUT/t" "$OUT/nic.txt"
cat "$OUT/nic.txt"
