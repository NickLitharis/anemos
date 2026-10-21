#!/usr/bin/env bash
# P3.4: run the CSP SPI harness inside XP itself.
#
# The provider is loaded with LoadLibrary, not CryptAcquireContext: stock XP
# verifies the provider image signature before calling CPAcquireContext, and an
# unsigned DLL cannot pass that (docs/P3-CRYPTO.md). This gate is therefore
# about the implementation and the ABI on the real kernel, not about Microsoft
# trusting the file.
#
# Two files must reach the guest, and ntfspatch can only overwrite existing
# files (it cannot create one). So each gets a temporary victim:
#   - the harness (~206 KB) over a large system32 executable;
#   - the provider (~3.6 MB) over a large non-boot file in the Windows tree.
# Victims must be uncompressed (a raw write into an NTFS-compressed file reads
# back as garbage) and must not be loaded during boot, or the fake file would
# break the boot it is supposed to observe. Both are backed up host-side first
# and restored on every exit path.
#
# The harness writes \conftest\spi.log; the conformance runner boots the disk,
# types the command into the Run box (no ':' or '%' travels), waits for the
# console to close, powers off, and extracts the log. The PASS/FAIL lines must
# match the host run exactly.
#
# Prereqs: the VM off, a writable VMDK next to harness/conformance's xp.vmx,
# and a green host gate (run.sh). The VM never touches the network.
#
# usage: xp_run.sh [--check <guest-log>]
set -uo pipefail

HERE=$(cd "$(dirname "$0")" && pwd)
REPO=$(cd "$HERE/../.." && pwd)
OUT="$HERE/out"
DISK="${XP_SUBJECT_DISK:-$HOME/vms/xpvmware/xp.vmdk}"
NTFS="$REPO/tools/ntfspatch.py"
RUNNER="$REPO/harness/conformance/runner.py"
GUEST_LOG='\conftest\spi.log'
SETTLE="${SETTLE:-900}"

die() { printf 'ERROR: %s\n' "$*" >&2; exit 1; }
say() { printf '%s\n' "$*"; }

# Large, uncompressed, ASCII-path files that normal boot does not read.
# oembios.bin is the last resort: it holds the OEM boot-logo data, so a fake
# file may disturb the splash (never the running system) if everything else is
# too small.
DLL_CANDIDATES=(
    '\windows\repair\software'
    '\windows\PCHealth\HelpCtr\Database\HCdata.edb'
    '\windows\srchasst\nls302en.lex'
    '\windows\system32\oembios.bin'
)
EXE_CANDIDATES=(
    '\windows\system32\mspaint.exe'
    '\windows\system32\ntbackup.exe'
    '\windows\system32\mstsc.exe'
)

stat_line() {
    python3 "$NTFS" "$DISK" stat "$1" 2>/dev/null
}

# choose <need-bytes> <path>... — first existing victim that is uncompressed,
# unsparse, and at least <need-bytes> allocated. Prints the path.
choose() {
    local need=$1 line real alloc flags path
    shift
    for path in "$@"; do
        line=$(stat_line "$path") || continue
        # stat_line prints: size <n> alloc <m> flags 0x<hex>
        read -r _ real _ alloc _ flags <<<"$line"
        [ -n "${alloc:-}" ] || continue
        [ $((flags & 1)) -eq 0 ] || continue          # compressed
        [ $((flags & 0x8000)) -eq 0 ] || continue     # sparse
        if [ "$alloc" -ge "$need" ]; then
            printf '%s\n' "$path"
            return 0
        fi
    done
    return 1
}

compare_logs() {
    local host=$1 guest=$2 work=$3
    # The guest writes its file in text mode (CRLF); the host baseline is
    # stdout already stripped of CR. Normalize both before comparing.
    tr -d '\r' <"$host" | grep -E '^(PASS|FAIL) ' >"$work/host-checks.txt" \
        || die "host log $host has no PASS/FAIL lines"
    tr -d '\r' <"$guest" | grep -E '^(PASS|FAIL) ' >"$work/guest-checks.txt" \
        || die "guest log $guest has no PASS/FAIL lines"
    if ! diff -u "$work/host-checks.txt" "$work/guest-checks.txt"; then
        die "guest result differs from the host run (see $work)"
    fi
    [ "$(wc -l <"$work/host-checks.txt")" -gt 0 ] \
        || die "zero checks compared — the comparison cannot fail"
    tr -d '\r' <"$guest" | grep -qx 'all CSP SPI checks pass' \
        || die "guest did not report success: $guest"
    say "guest PASS/FAIL lines are identical to the host run"
}

# --check: compare an already-collected guest log, no VM.
if [ "${1:-}" = "--check" ]; then
    [ $# -eq 2 ] || die "usage: xp_run.sh --check <guest-log>"
    [ -f "$2" ] || die "no such log: $2"
    [ -f "$OUT/spi.log" ] || die "no host baseline at $OUT/spi.log — run run.sh"
    mkdir -p "$OUT/xp-check"
    compare_logs "$OUT/spi.log" "$2" "$OUT/xp-check"
    exit 0
fi
[ $# -eq 0 ] || die "usage: xp_run.sh [--check <guest-log>]"

# 1. Host gate first: a guest run is only interesting when the same binary
#    passes on the host, and run.sh produces out/spi.log as the baseline.
say "== host gate and build =="
"$HERE/run.sh" >"$OUT/xp-host-run.log" 2>&1 \
    || die "host gate failed; see $OUT/xp-host-run.log"
[ -f "$OUT/anemosp.dll" ] && [ -f "$OUT/spi_test.exe" ] \
    || die "host gate did not produce the DLL and harness"
[ -f "$OUT/spi.log" ] || die "host gate did not produce out/spi.log"

EXE_SIZE=$(stat -c%s "$OUT/spi_test.exe")
DLL_SIZE=$(stat -c%s "$OUT/anemosp.dll")
say "harness $EXE_SIZE bytes, provider $DLL_SIZE bytes"

# 2. The guest must be off: the disk is patched host-side.
running=$(vmrun -T ws list | tail -n +2)
[ -z "$running" ] || die "a VM is running — power it off first: $running"
# -w is not enough: a filesystem remounted read-only still advertises mode bits.
python3 - "$DISK" <<'PY' 2>/dev/null || die "$DISK is not writable (host filesystem read-only?)"
import sys
try:
    open(sys.argv[1], 'r+b').close()
except OSError:
    raise SystemExit(1)
PY

EXE_VICTIM=$(choose "$EXE_SIZE" "${EXE_CANDIDATES[@]}") \
    || die "no uncompressed system32 exe is large enough (need $EXE_SIZE)"
DLL_VICTIM=$(choose "$DLL_SIZE" "${DLL_CANDIDATES[@]}") \
    || die "no uncompressed non-boot file is large enough (need $DLL_SIZE)"
[ "$EXE_VICTIM" != "$DLL_VICTIM" ] || die "victims collide"

# 3. Back up both victims before touching anything, then arm the trap. An
#    interrupted run must leave the subject disk exactly as it was.
EXE_BAK="$OUT/victim-exe.bin"
DLL_BAK="$OUT/victim-dll.bin"
python3 "$NTFS" "$DISK" get "$EXE_VICTIM" "$EXE_BAK" \
    || die "cannot read $EXE_VICTIM"
python3 "$NTFS" "$DISK" get "$DLL_VICTIM" "$DLL_BAK" \
    || die "cannot read $DLL_VICTIM"

restore_one() {
    local path=$1 bak=$2
    [ -f "$bak" ] || return 0
    python3 "$NTFS" "$DISK" put "$path" "$bak" \
        || die "restore of $path FAILED — the victim is still patched"
    rm -f "$bak"
    say "restored $path"
}
restore() { restore_one "$EXE_VICTIM" "$EXE_BAK"; restore_one "$DLL_VICTIM" "$DLL_BAK"; }
trap restore EXIT

say "exe victim: $EXE_VICTIM"
say "dll victim: $DLL_VICTIM"
python3 "$NTFS" "$DISK" put "$EXE_VICTIM" "$OUT/spi_test.exe" \
    || die "cannot install the harness over $EXE_VICTIM"
python3 "$NTFS" "$DISK" put "$DLL_VICTIM" "$OUT/anemosp.dll" \
    || die "cannot install the provider over $DLL_VICTIM"

# 4. Launch through the Run box. The Run box resolves the exe in system32;
#    both arguments are absolute and colon-free. The harness briefly keeps its
#    console up so the runner's pixel probe can see the launch.
EXE_NAME=${EXE_VICTIM##*\\}
TAG="csp-xp-$(date +%Y%m%d-%H%M%S)"
say "== boot, launch, collect (tag $TAG) =="
python3 "$RUNNER" extract --disk "$DISK" --tag "$TAG" \
    --exe "$EXE_NAME $DLL_VICTIM $GUEST_LOG" \
    --collect spi.log --settle "$SETTLE" \
    || die "runner extract failed (tag $TAG)"

GUEST_COPY="$REPO/harness/conformance/out/$TAG/guest-spi.log"
[ -f "$GUEST_COPY" ] || die "no spi.log collected (tag $TAG) — did the harness run?"

# 5. Restore immediately, then compare. The gate is identical PASS/FAIL lines.
restore
trap - EXIT
mkdir -p "$OUT/xp-guest"
cp "$GUEST_COPY" "$OUT/xp-guest/spi.log"
compare_logs "$OUT/spi.log" "$GUEST_COPY" "$OUT/xp-guest"
say ""
say "P3.4 gate passed. Artifacts: $OUT/xp-guest/spi.log, $OUT/xp-host-run.log"
