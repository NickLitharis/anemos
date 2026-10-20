#!/usr/bin/env bash
# P1: STATUS->DOS map — Anemos's model for ntdll!RtlNtStatusToDosError.
#
# The model is two rules (customer identity, default 317) plus a learned
# exception table (model.txt). This suite is the host-side half of its
# verification; the other half is one in-guest run whose certificate proves
# the model matches XP for all 2^32 statuses. Nine checks, each of which
# can fail:
#   1  gen_model.py must ACCEPT model.txt (ascending, no redundant entries,
#      all published anchors)
#   2  the boundary suite (pinned clang, i686 PE, Wine) must pass
#   3  the same suite with the binary-search off-by-one (-DANEM_STATUSDOS_*
#      NEGATIVE) must FAIL
#   4  verify_prefix: every status in the XP dump's valid prefix compared
#      against the model, natively (~539M statuses from the one genuine run)
#   5  the negative verify_prefix must FAIL
#   6  statusdos.c compiles warning-free freestanding for i686-windows-msvc
#   7  mv_probe.c builds as a freestanding i686 PE that fits the victim exe
#      it gets swapped into inside the guest (checked against the real
#      allocation read from the subject disk)
#   8  the in-guest certificate for THIS table must exist and say VERIFIED —
#      a missing, stale or divergent certificate fails the unit, because a
#      model XP has never judged is not a verified model
#   9  selftest.sh: the whole loop (walk -> DIVERGENT -> learn -> regenerate
#      -> walk -> VERIFIED, plus both stale-certificate refusals) against a
#      synthetic XP — the checker and learner must work, or step 8's verdict
#      means nothing
#
# usage: anemos/statusdos/run.sh [sdos-cert.bin] [sdos.bin]
#   arg 1: certificate from an in-guest run (default: cert/sdos-cert.bin,
#          which is committed once obtained — it is 72 bytes when VERIFIED)
#   arg 2: raw prefix dump from the sdos-ext2 run, if present locally
#          (untracked, ~381 MB; step 4 is SKIPPED without it, loudly)
#
# exit 0 = unit passes, including the XP certificate gate
set -uo pipefail

HERE=$(cd "$(dirname "$0")" && pwd)
REPO=$(cd "$HERE/../.." && pwd)
OUT="$HERE/out"
LLVM="$HOME/.local/opt/llvm-mingw-20261006-msvcrt-ubuntu-22.04-x86_64/bin"
CERT="${1:-$HERE/cert/sdos-cert.bin}"
DUMP="${2:-$REPO/harness/conformance/out/sdos-ext2/guest-sdos.bin}"
DISK="${XP_SUBJECT_DISK:-$HOME/vms/xpvmware/xp.vmdk}"
VICTIM='windows\system32\notepad.exe'   # the exe the probe is swapped into

export PATH="$LLVM:$PATH"
export WINEDEBUG=-all WINEPREFIX=${WINEPREFIX:-$HOME/.wine-xp}

FLAGS=(-std=c23 -Oz -Wall -Wextra -Werror -Wconversion -Wshadow -Wcast-align
       -Wstrict-prototypes -Wmissing-prototypes -Wnull-dereference)

pass=0
fail=0
skip=0
ok()  { pass=$((pass + 1)); printf 'PASS  %s\n' "$1"; }
bad() { fail=$((fail + 1)); printf 'FAIL  %s\n' "$1"; }
skp() { skip=$((skip + 1)); printf 'SKIP  %s\n' "$1"; }
step() { printf '\n== %s ==\n' "$1"; }

mkdir -p "$OUT"

step "1: gen_model.py accepts model.txt (ascending, no redundancy, anchors)"
# Fatal on failure: every later step must run against a table generated from
# the model being tested, never a stale one left in out/.
if python3 "$HERE/gen_model.py" "$HERE/model.txt" "$OUT/table.c" \
        | tee "$OUT/gen.log"; then
    ok "model is well-formed and anchor-consistent"
else
    bad "model is well-formed and anchor-consistent"
    printf '\n%d passed, %d failed, %d skipped\n' "$pass" "$fail" "$skip"
    exit 1
fi

step "2: boundary + anchor suite (pinned clang, i686 PE, Wine)"
# Windows accepts '/' in paths, so the POSIX path rides straight through.
if clang --target=i686-w64-mingw32 "${FLAGS[@]}" -I "$HERE" \
        -o "$OUT/sdostest.exe" "$HERE/statusdos.c" "$OUT/table.c" \
        "$HERE/test_statusdos.c"; then
    ok "clang builds the suite"
    if wine "$OUT/sdostest.exe" 2>&1 | tr -d '\r' | tee "$OUT/sdostest.log"; then
        ok "suite passes"
    else
        bad "suite passes"
    fi
else
    bad "clang builds the suite"
fi

step "3: negative control — off-by-one binary search must fail"
if clang --target=i686-w64-mingw32 "${FLAGS[@]}" -DANEM_STATUSDOS_NEGATIVE \
        -I "$HERE" \
        -o "$OUT/sdosneg.exe" "$HERE/statusdos.c" "$OUT/table.c" \
        "$HERE/test_statusdos.c"; then
    ok "off-by-one build compiles"
    if wine "$OUT/sdosneg.exe" >/dev/null 2>&1; then
        bad "off-by-one build is rejected"
    else
        ok "off-by-one build is rejected"
    fi
else
    bad "off-by-one build compiles"
fi

step "4: prefix differential — every status the XP dump covers (native)"
if [ -f "$DUMP" ]; then
    if clang "${FLAGS[@]}" -I "$HERE" -o "$OUT/verify_prefix" \
            "$HERE/statusdos.c" "$OUT/table.c" "$HERE/verify_prefix.c"; then
        ok "native verify_prefix builds"
        if "$OUT/verify_prefix" "$DUMP" | tee "$OUT/verify_prefix.log"; then
            ok "every status in the dump's valid prefix matches XP"
        else
            bad "every status in the dump's valid prefix matches XP"
        fi
    else
        bad "native verify_prefix builds"
    fi
else
    skp "prefix differential — no dump at $DUMP (untracked, ~381 MB; the"
    printf '     in-guest certificate in step 8 is the full-2^32 evidence)\n'
fi

step "5: negative control — the prefix differential must fail"
if [ -f "$DUMP" ]; then
    if clang "${FLAGS[@]}" -DANEM_STATUSDOS_NEGATIVE -I "$HERE" \
            -o "$OUT/verify_prefix_neg" \
            "$HERE/statusdos.c" "$OUT/table.c" "$HERE/verify_prefix.c"; then
        ok "negative verify_prefix builds"
        if "$OUT/verify_prefix_neg" "$DUMP" >/dev/null 2>&1; then
            bad "negative verify_prefix is rejected"
        else
            ok "negative verify_prefix is rejected"
        fi
    else
        bad "negative verify_prefix builds"
    fi
else
    skp "negative prefix differential — same missing dump"
fi

step "6: freestanding i686-windows-msvc object (the destination ABI)"
if clang --target=i686-windows-msvc -ffreestanding -fno-builtin \
        -fno-stack-protector "${FLAGS[@]}" \
        -c "$HERE/statusdos.c" -o "$OUT/statusdos.i686.obj"; then
    ok "statusdos.c is freestanding-clean for i686-windows-msvc"
else
    bad "statusdos.c is freestanding-clean for i686-windows-msvc"
fi

step "7: the probe builds freestanding and fits its guest victim"
# The probe is swapped into the victim exe host-side; if it outgrows the
# victim's NTFS allocation the swap fails mid-recipe, so check up front.
if clang --target=i686-w64-mingw32 -nostdlib -ffreestanding -fno-builtin \
        -fno-stack-protector "${FLAGS[@]}" -I "$HERE" \
        -o "$OUT/mv_probe.exe" "$HERE/mv_probe.c" "$HERE/statusdos.c" \
        "$OUT/table.c" -Wl,-e,_mainCRTStartup \
        -Xlinker /subsystem:console,5.01 -lntdll -lkernel32 -s; then
    ok "mv_probe.exe builds (freestanding, console 5.01)"
    probe_size=$(stat -c %s "$OUT/mv_probe.exe")
    alloc=$(python3 - "$REPO" "$DISK" "$VICTIM" <<'PY'
import sys, struct
sys.path.insert(0, sys.argv[1] + "/tools")
try:
    from ntfspatch import Vmdk, NTFS
    nt = NTFS(Vmdk(sys.argv[2]), 63)
    rec = nt.record(nt.lookup("\\" + sys.argv[3]))
    a = nt._attr(rec, 0x80)
    print(struct.unpack('<Q', a[40:48])[0])
except Exception as e:
    print(f"unavailable: {e}")
PY
)
    if [[ "$alloc" =~ ^[0-9]+$ ]]; then
        if [ "$probe_size" -le "$alloc" ]; then
            ok "probe is $probe_size B, victim $VICTIM allocates $alloc B"
        else
            bad "probe is $probe_size B but victim allocates only $alloc B"
        fi
    else
        # Disk absent (e.g. CI): fall back to the size the victim is known
        # to allocate, so the recipe is still checked against something.
        known=69632   # notepad.exe on the subject disk, measured 2026-10-19
        if [ "$probe_size" -le "$known" ]; then
            ok "probe is $probe_size B <= $known B (victim alloc; disk "
            printf '     unavailable: %s)\n' "$alloc"
        else
            bad "probe is $probe_size B > $known B and the disk is unreadable"
        fi
    fi
else
    bad "mv_probe.exe builds (freestanding, console 5.01)"
fi

step "8: the in-guest certificate for this table says VERIFIED"
# The heart of the unit: XP itself judged this exact table over all 2^32
# statuses. A missing or stale certificate fails — an unjudged model is not
# a verified model, and the table's CRC is what ties certificate to table.
table_crc=$(cat "$OUT/table.c.crc")
if [ ! -f "$CERT" ]; then
    bad "certificate exists at $CERT"
    printf '     none found. Produce it with an in-guest run:\n'
    printf '       anemos/statusdos/mv_run.sh --install   # one VM boot\n'
    printf '     (the certificate is committed under cert/ once VERIFIED;\n'
    printf '      it is 72 bytes — header + empty body + footer)\n'
elif python3 "$HERE/check_cert.py" "$HERE/model.txt" "$CERT" \
        | tee "$OUT/cert.log"; then
    ok "certificate VERIFIED (table crc32 $table_crc, all 2^32 statuses)"
else
    bad "certificate VERIFIED (table crc32 $table_crc)"
    printf '     divergent or stale: fold the corrections with\n'
    printf '       learn.py model.txt <cert> --write\n'
    printf '     then re-run this suite (the table changed)\n'
fi

step "9: the verification loop itself converges (no VM, synthetic XP)"
# Steps 1-8 test the model; this tests the machinery that judges it. A
# broken checker or learner must not stay hidden until someone boots the VM.
if "$HERE/selftest.sh" >"$OUT/selftest.log" 2>&1; then
    ok "walk -> DIVERGENT -> learn -> walk -> VERIFIED, plus both refusals"
else
    bad "walk -> DIVERGENT -> learn -> walk -> VERIFIED, plus both refusals"
    tail -30 "$OUT/selftest.log"
fi

printf '\n%d passed, %d failed, %d skipped\n' "$pass" "$fail" "$skip"
[ "$fail" -eq 0 ] || exit 1
exit 0