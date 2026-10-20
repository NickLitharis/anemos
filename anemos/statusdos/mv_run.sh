#!/usr/bin/env bash
# One in-guest model-verification run: swap mv_probe.exe into an expendable
# system32 exe on the SUBJECT disk, boot XP, let the probe compare the
# shipping model against ntdll!RtlNtStatusToDosError for all 2^32 statuses,
# collect the certificate, restore the victim byte-identical, and check it.
#
# This is the full-2^32 half of the unit's verification; run.sh step 8 gates
# on the certificate this produces. Everything here is host-side except the
# single VM boot; the probe itself writes kilobytes, not gigabytes.
#
# The victim is notepad.exe (69,632 B allocated — headroom for a table that
# grows as we learn), launched by typing its name in the Run box. The victim
# is backed up before the swap and restored after, on every path including
# failure: the trap is what makes "the guest is left as found" a property of
# the script, not of the operator's attention.
#
# Prerequisites: VM off (the runner enforces exclusivity), tools/ntfspatch.py
# for the swap, vmrun+vncdo for the boot (harness/conformance/runner.py).
#
# usage:
#   mv_run.sh --install          swap in, boot, collect, restore, check
#   mv_run.sh --restore          restore the victim from the backup only
#                                (use if a run was interrupted)
#   mv_run.sh --check <cert>     check an existing certificate, no VM
set -uo pipefail

HERE=$(cd "$(dirname "$0")" && pwd)
REPO=$(cd "$HERE/../.." && pwd)
OUT="$HERE/out"
LLVM="$HOME/.local/opt/llvm-mingw-20261006-msvcrt-ubuntu-22.04-x86_64/bin"
DISK="${XP_SUBJECT_DISK:-$HOME/vms/xpvmware/xp.vmdk}"
VICTIM_PATH='\windows\system32\notepad.exe'
VICTIM_NAME='notepad'                    # what the Run box is typed with
BACKUP="$OUT/notepad-orig.exe"
CERT_OUT="$HERE/cert/sdos-cert.bin"
# The probe walks 2^32 statuses in-guest, calling ntdll once per status. A
# cold single-vCPU XP in VMware needed more than an hour for that on the
# first attempt (settle 3600 expired mid-walk with the probe still running),
# so the default is three hours. The runner polls for the probe's console to
# close and proceeds the moment it does, so this is a ceiling, not a delay;
# the probe's heartbeat (one line per 2^28 statuses) shows the pace.
SETTLE="${SETTLE:-10800}"

export PATH="$LLVM:$PATH"

die() { printf 'ERROR: %s\n' "$*" >&2; exit 1; }

restore_victim() {
    if [ -f "$BACKUP" ]; then
        python3 "$REPO/tools/ntfspatch.py" "$DISK" put "$VICTIM_PATH" \
            "$BACKUP" || die "restore of $VICTIM_PATH FAILED — the subject "
        printf 'restored %s from %s (byte-identical)\n' "$VICTIM_PATH" "$BACKUP"
    else
        printf 'no backup at %s — nothing to restore\n' "$BACKUP"
    fi
}

case "${1:-}" in
--check)
    [ $# -eq 2 ] || die "usage: mv_run.sh --check <sdos-cert.bin>"
    exec python3 "$HERE/check_cert.py" "$HERE/model.txt" "$1"
    ;;
--restore)
    restore_victim
    exit 0
    ;;
--install) ;;
*) die "usage: mv_run.sh --install | --restore | --check <cert>" ;;
esac

mkdir -p "$OUT" "$HERE/cert"

# 1. Generate the table from the model, then build the probe around it. The
#    probe embeds this exact table and its CRC; the certificate carries the
#    CRC back, which is how check_cert.py knows the two belong together.
python3 "$HERE/gen_model.py" "$HERE/model.txt" "$OUT/table.c" \
    || die "gen_model.py rejected model.txt"
clang --target=i686-w64-mingw32 -nostdlib -ffreestanding -fno-builtin \
    -fno-stack-protector -std=c23 -Oz -Wall -Wextra -Werror \
    -Wconversion -Wshadow -Wcast-align -Wstrict-prototypes \
    -Wmissing-prototypes -Wnull-dereference -I "$HERE" \
    -o "$OUT/mv_probe.exe" "$HERE/mv_probe.c" "$HERE/statusdos.c" \
    "$OUT/table.c" -Wl,-e,_mainCRTStartup \
    -Xlinker /subsystem:console,5.01 -lntdll -lkernel32 -s \
    || die "probe build failed"
printf 'probe: %s bytes, table crc32 %s\n' \
    "$(stat -c %s "$OUT/mv_probe.exe")" "$(cat "$OUT/table.c.crc")"

# 2. Back the victim up host-side BEFORE touching it. This is the only copy
#    that survives the swap; the trap below restores from it on every exit
#    path, so an interrupted run leaves the subject disk as it was.
python3 "$REPO/tools/ntfspatch.py" "$DISK" get "$VICTIM_PATH" "$BACKUP" \
    || die "cannot read $VICTIM_PATH from $DISK"
trap restore_victim EXIT

# 3. Swap the probe in and boot the guest. The runner types the victim's
#    name into the Run box (pixel-verified), waits for the probe's console
#    to close itself — the guest's own proof it finished and flushed — then
#    hard-powers-off and extracts \conftest\sdos-cert.bin host-side.
python3 "$REPO/tools/ntfspatch.py" "$DISK" put "$VICTIM_PATH" \
    "$OUT/mv_probe.exe" || die "cannot swap probe into $VICTIM_PATH"

TAG="mv-$(date +%Y%m%d-%H%M%S)"
python3 "$REPO/harness/conformance/runner.py" extract \
    --disk "$DISK" --tag "$TAG" --exe "$VICTIM_NAME" \
    --collect sdos-cert.bin --settle "$SETTLE" \
    || die "runner extract failed (tag $TAG)"

# 4. Collect and check. The trap restores the victim as we leave.
GUEST_CERT="$REPO/harness/conformance/out/$TAG/guest-sdos-cert.bin"
[ -f "$GUEST_CERT" ] || die "no sdos-cert.bin collected (tag $TAG) — the "\
"probe did not finish; see $REPO/harness/conformance/out/$TAG"
cp "$GUEST_CERT" "$CERT_OUT"
printf 'certificate: %s (%s bytes)\n' "$CERT_OUT" "$(stat -c %s "$CERT_OUT")"
python3 "$HERE/check_cert.py" "$HERE/model.txt" "$CERT_OUT"
rc=$?
if [ $rc -eq 0 ]; then
    printf '\nVERIFIED. Commit it: cp %s %s/cert/ && git add anemos/statusdos\n' \
        "$CERT_OUT" "$HERE"
fi
exit $rc
