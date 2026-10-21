#!/usr/bin/env bash
# K1: install the driver in XP, ping it, unload it, collect the certificate.
# usage: m1_run.sh
set -uo pipefail
HERE=$(cd "$(dirname "$0")" && pwd)
REPO=$(cd "$HERE/../../.." && pwd)
OUT="$HERE/out/m1"
DISK="${XP_SUBJECT_DISK:-$HOME/vms/xpvmware/xp.vmdk}"
if [ -z "${ANEMOS_VM_DIR:-}" ] && [ -n "${XP_SUBJECT_DISK:-}" ]; then
    ANEMOS_VM_DIR=$(dirname "$DISK"); export ANEMOS_VM_DIR
fi
NTFS="$REPO/tools/ntfspatch.py"
RUNNER="$REPO/harness/conformance/runner.py"
VICTIM='\windows\system32\mspaint.exe'
GUEST_CERT='\conftest\anemos-m1.txt'
SETTLE="${SETTLE:-600}"
die() { printf 'ERROR: %s\n' "$*" >&2; exit 1; }
say() { printf '%s\n' "$*"; }

mkdir -p "$OUT"
"$HERE/m1_build.sh" >"$OUT/build.log" 2>&1 || die "build failed; see $OUT/build.log"
EXE="$HERE/out/m1_test.exe"
SIZE=$(stat -c%s "$EXE")
BAK="$OUT/victim-mspaint.exe"

restore() {
    if [ -f "$BAK" ]; then
        python3 "$NTFS" "$DISK" put "$VICTIM" "$BAK" \
            || die "restore of $VICTIM FAILED"
        rm -f "$BAK"
        say "restored $VICTIM"
    fi
}

running=$(vmrun -T ws list | tail -n +2)
[ -z "$running" ] || die "a VM is running: $running"
python3 "$NTFS" "$DISK" get "$VICTIM" "$BAK" || die "cannot read $VICTIM"
truncate -s "$SIZE" "$BAK"
trap restore EXIT
python3 "$NTFS" "$DISK" put "$VICTIM" "$EXE" || die "cannot install the test"

TAG="net-m1-$(date +%Y%m%d-%H%M%S)"
say "== boot, install, ping, unload (tag $TAG) =="
python3 "$RUNNER" extract --disk "$DISK" --tag "$TAG" \
    --exe "mspaint.exe $GUEST_CERT" --collect anemos-m1.txt \
    --settle "$SETTLE" --shutdown-grace 300 \
    || die "runner extract failed (tag $TAG)"
GUEST_COPY="$REPO/harness/conformance/out/$TAG/guest-anemos-m1.txt"
[ -f "$GUEST_COPY" ] || die "no certificate collected (tag $TAG)"
restore
trap - EXIT
cp "$GUEST_COPY" "$OUT/anemos-m1.txt"
tr -d '\r' <"$OUT/anemos-m1.txt" >"$OUT/t" && mv "$OUT/t" "$OUT/anemos-m1.txt"
grep -qx 'all kernel M1 observations complete' "$OUT/anemos-m1.txt" \
    || die "incomplete certificate"
say "certificate: $OUT/anemos-m1.txt"
cat "$OUT/anemos-m1.txt"
for want in "service.start 1" "ioctl.ping 1" "ioctl.signature 1095648589" "selftest.rc 1" "selftest.match 1" "ndis.rc 1" "service.stop 1"; do
    grep -qx "$want" "$OUT/anemos-m1.txt" \
        || die "K1 gate failed: missing '$want'"
done
say "K2 PASS: driver loaded, ping returned ANEM, kernel lwIP self-test matched, driver unloaded (NDIS registration reported; binding tracked in docs)"
