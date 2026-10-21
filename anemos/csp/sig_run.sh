#!/usr/bin/env bash
# P3.5 experiment: try to load the provider through CryptoAPI on stock XP.
#
# Installs the probe and the provider over two temporary victims, registers
# the provider in-guest, tries the documented flag combinations, collects the
# certificate, and restores both victims. Nothing here patches advapi32.
#
# usage: sig_run.sh [--restore]
set -uo pipefail

HERE=$(cd "$(dirname "$0")" && pwd)
REPO=$(cd "$HERE/../.." && pwd)
OUT="$HERE/out/sig"
DISK="${XP_SUBJECT_DISK:-$HOME/vms/xpvmware/xp.vmdk}"
if [ -z "${ANEMOS_VM_DIR:-}" ] && [ -n "${XP_SUBJECT_DISK:-}" ]; then
    ANEMOS_VM_DIR=$(dirname "$DISK")
    export ANEMOS_VM_DIR
fi
NTFS="$REPO/tools/ntfspatch.py"
RUNNER="$REPO/harness/conformance/runner.py"
LLVM="$HOME/.local/opt/llvm-mingw-20261006-msvcrt-ubuntu-22.04-x86_64/bin"
GUEST_CERT='\conftest\csp-sig.txt'
EXE_VICTIM='\windows\system32\mspaint.exe'
EXE_NAME='mspaint.exe'
DLL_VICTIM='\windows\PCHealth\HelpCtr\Database\HCdata.edb'
SETTLE="${SETTLE:-600}"

export PATH="$LLVM:$PATH"
die() { printf 'ERROR: %s\n' "$*" >&2; exit 1; }
say() { printf '%s\n' "$*"; }

mkdir -p "$OUT" "$HERE/out"
EXE_SIZE=$(stat -c%s "$HERE/out/spi_test.exe" 2>/dev/null || echo 0)
DLL_SIZE=$(stat -c%s "$HERE/out/anemosp.dll" 2>/dev/null || echo 0)
EXE_BAK="$OUT/victim-exe.bin"
DLL_BAK="$OUT/victim-dll.bin"

restore() {
    if [ -f "$EXE_BAK" ]; then
        python3 "$NTFS" "$DISK" put "$EXE_VICTIM" "$EXE_BAK" \
            || die "restore of $EXE_VICTIM FAILED"
        rm -f "$EXE_BAK"
        say "restored $EXE_VICTIM"
    fi
    if [ -f "$DLL_BAK" ]; then
        python3 "$NTFS" "$DISK" put "$DLL_VICTIM" "$DLL_BAK" \
            || die "restore of $DLL_VICTIM FAILED"
        rm -f "$DLL_BAK"
        say "restored $DLL_VICTIM"
    fi
}

if [ "${1:-}" = "--restore" ]; then
    restore
    exit 0
fi
[ $# -eq 0 ] || die "usage: sig_run.sh [--restore]"

say "== build =="
"$HERE/run.sh" >"$OUT/host-gate.log" 2>&1 \
    || die "host CSP gate failed; see $OUT/host-gate.log"
i686-w64-mingw32-clang -std=c23 -Oz -Wall -Wextra -Werror \
    -o "$HERE/out/sig_probe.exe" "$HERE/sig_probe.c" -ladvapi32 \
    -Xlinker /subsystem:console,5.01 \
    || die "signature probe build failed"
python3 "$HERE/../csp/check_xp_imports.py" "$HERE/out/sig_probe.exe" \
    --objdump "$LLVM/llvm-objdump" >/dev/null \
    || die "signature probe imports an API XP does not have"
EXE_SIZE=$(stat -c%s "$HERE/out/sig_probe.exe")
[ "$DLL_SIZE" -gt 0 ] || die "no provider DLL built"

running=$(vmrun -T ws list | tail -n +2)
[ -z "$running" ] || die "a VM is running — power it off first: $running"
python3 - "$DISK" <<'PY' 2>/dev/null || die "$DISK is not writable"
import sys
try:
    open(sys.argv[1], 'r+b').close()
except OSError:
    raise SystemExit(1)
PY

python3 "$NTFS" "$DISK" get "$EXE_VICTIM" "$EXE_BAK" || die "cannot read $EXE_VICTIM"
python3 "$NTFS" "$DISK" get "$DLL_VICTIM" "$DLL_BAK" || die "cannot read $DLL_VICTIM"
truncate -s "$EXE_SIZE" "$EXE_BAK"
truncate -s "$DLL_SIZE" "$DLL_BAK"
trap restore EXIT

python3 "$NTFS" "$DISK" put "$EXE_VICTIM" "$HERE/out/sig_probe.exe" \
    || die "cannot install the probe"
python3 "$NTFS" "$DISK" put "$DLL_VICTIM" "$HERE/out/anemosp.dll" \
    || die "cannot install the provider"

TAG="csp-sig-$(date +%Y%m%d-%H%M%S)"
say "== boot, register, probe (tag $TAG) =="
python3 "$RUNNER" extract --disk "$DISK" --tag "$TAG" \
    --exe "$EXE_NAME $DLL_VICTIM $GUEST_CERT" \
    --collect csp-sig.txt --settle "$SETTLE" --shutdown-grace 300 \
    || die "runner extract failed (tag $TAG)"

GUEST_COPY="$REPO/harness/conformance/out/$TAG/guest-csp-sig.txt"
[ -f "$GUEST_COPY" ] || die "no certificate collected (tag $TAG)"
restore
trap - EXIT
cp "$GUEST_COPY" "$OUT/csp-sig.txt"
tr -d '\r' <"$OUT/csp-sig.txt" >"$OUT/csp-sig.tmp" && mv "$OUT/csp-sig.tmp" "$OUT/csp-sig.txt"
grep -qx 'all signature observations complete' "$OUT/csp-sig.txt" \
    || die "incomplete certificate: $OUT/csp-sig.txt"
say "certificate: $OUT/csp-sig.txt"
grep '^acquire\.' "$OUT/csp-sig.txt" || true
