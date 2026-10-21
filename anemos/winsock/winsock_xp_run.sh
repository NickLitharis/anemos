#!/usr/bin/env bash
# P4.6 capture: run the Winsock observation probe inside XP and collect its
# certificate. The comparison against our implementation is a separate step
# once the host-side twin exists.
#
# One victim is needed: the probe is a single console exe and writes
# \conftest\winsock-cert.txt. It is backed up host-side and restored on every
# exit path. The VM must be off and the VMDK writable; see xp_run.sh for the
# same mechanics.
#
# usage:
#   winsock_xp_run.sh            boot, capture, restore
#   winsock_xp_run.sh --restore  restore the victim only
set -uo pipefail

HERE=$(cd "$(dirname "$0")" && pwd)
REPO=$(cd "$HERE/../.." && pwd)
OUT="$HERE/out/xp"
DISK="${XP_SUBJECT_DISK:-$HOME/vms/xpvmware/xp.vmdk}"
if [ -z "${ANEMOS_VM_DIR:-}" ] && [ -n "${XP_SUBJECT_DISK:-}" ]; then
    ANEMOS_VM_DIR=$(dirname "$DISK")
    export ANEMOS_VM_DIR
fi
NTFS="$REPO/tools/ntfspatch.py"
RUNNER="$REPO/harness/conformance/runner.py"
LLVM="$HOME/.local/opt/llvm-mingw-20261006-msvcrt-ubuntu-22.04-x86_64/bin"
GUEST_CERT='\conftest\winsock-cert.txt'
VICTIM='\windows\system32\mspaint.exe'
VICTIM_NAME='mspaint.exe'
BACKUP="$OUT/victim-mspaint.exe"
SETTLE="${SETTLE:-600}"

export PATH="$LLVM:$PATH"
die() { printf 'ERROR: %s\n' "$*" >&2; exit 1; }
say() { printf '%s\n' "$*"; }

restore_victim() {
    if [ -f "$BACKUP" ]; then
        python3 "$NTFS" "$DISK" put "$VICTIM" "$BACKUP" \
            || die "restore of $VICTIM FAILED"
        rm -f "$BACKUP"
        say "restored $VICTIM"
    fi
}

if [ "${1:-}" = "--restore" ]; then
    restore_victim
    exit 0
fi
[ $# -eq 0 ] || die "usage: winsock_xp_run.sh [--restore]"

mkdir -p "$OUT" "$HERE/out"
say "== build =="
i686-w64-mingw32-clang -std=c23 -Oz -Wall -Wextra -Werror \
    -o "$HERE/out/xp_probe.exe" "$HERE/xp_probe.c" -lws2_32 \
    || die "probe build failed"
python3 "$HERE/../csp/check_xp_imports.py" "$HERE/out/xp_probe.exe" \
    --objdump "$LLVM/llvm-objdump" >/dev/null \
    || die "probe imports an API XP does not have"

running=$(vmrun -T ws list | tail -n +2)
[ -z "$running" ] || die "a VM is running — power it off first: $running"
python3 - "$DISK" <<'PY' 2>/dev/null || die "$DISK is not writable"
import sys
try:
    open(sys.argv[1], 'r+b').close()
except OSError:
    raise SystemExit(1)
PY

python3 "$NTFS" "$DISK" get "$VICTIM" "$BACKUP" || die "cannot read $VICTIM"
trap restore_victim EXIT
say "victim: $VICTIM"
python3 "$NTFS" "$DISK" put "$VICTIM" "$HERE/out/xp_probe.exe" \
    || die "cannot install the probe"

TAG="ws-xp-$(date +%Y%m%d-%H%M%S)"
say "== boot, launch, collect (tag $TAG) =="
python3 "$RUNNER" extract --disk "$DISK" --tag "$TAG" \
    --exe "$VICTIM_NAME $GUEST_CERT" --collect winsock-cert.txt \
    --settle "$SETTLE" \
    || die "runner extract failed (tag $TAG)"

GUEST_COPY="$REPO/harness/conformance/out/$TAG/guest-winsock-cert.txt"
[ -f "$GUEST_COPY" ] || die "no certificate collected (tag $TAG)"
restore_victim
trap - EXIT
cp "$GUEST_COPY" "$OUT/winsock-cert.txt"
# The guest writes CRLF; normalize once so every later check sees LF.
tr -d '\r' <"$OUT/winsock-cert.txt" >"$OUT/winsock-cert.tmp" \
    && mv "$OUT/winsock-cert.tmp" "$OUT/winsock-cert.txt"
grep -qx 'all winsock observations complete' "$OUT/winsock-cert.txt" \
    || die "incomplete certificate: $OUT/winsock-cert.txt"
cases=$(grep -c ' ' "$OUT/winsock-cert.txt")
say "certificate: $OUT/winsock-cert.txt ($cases case lines)"
say "next: build the host-side twin and run conformance_compare.py"
