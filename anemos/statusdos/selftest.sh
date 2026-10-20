#!/usr/bin/env bash
# Self-test of the model-verification loop: walk -> refuse -> learn ->
# regenerate -> re-walk, against a synthetic XP — no VM, no certificate from
# a real guest needed.
#
# How it runs the real probe without Windows: mv_probe.c's platform block is
# swapped for POSIX stand-ins (-DANEM_MV_NATIVE -> selftest_shim.h), so the
# walk, the emit and the footer are the SAME source that runs inside the
# guest — one writer of the certificate format, not a test-only copy that
# could drift from it. The oracle is selftest_oracle.c, which answers exactly
# like the current model except at five planted statuses (one per quadrant,
# plus an adjacent pair), standing in for table entries XP has and we have
# not learned yet.
#
# What is asserted, each of which can fail:
#   1  walk 1 finds exactly the five planted mismatches (exit 3) and writes a
#      132-byte certificate — 16 header + 5*12 records + 56 footer
#   2  check_cert.py says DIVERGENT, not VERIFIED, on that certificate
#   3  learn.py's dry run reports 5 entry changes and does NOT touch its input
#   4  learn.py --write folds them into a SCRATCH copy of model.txt
#      (17 -> 22 entries); the real model.txt must come out byte-identical
#   5  gen_model.py accepts the learned model and the table CRC changes
#   6  walk 2 finds zero mismatches (exit 0): a 72-byte VERIFIED certificate
#   7  check_cert.py says VERIFIED on it
#   8  negative control: each walk's certificate is REFUSED against the other
#      model (stale entry count), so a green verdict needs the matching table
#
# usage: anemos/statusdos/selftest.sh      (exit 0 = the loop converges)
set -uo pipefail

HERE=$(cd "$(dirname "$0")" && pwd)
SCRATCH="$HERE/out/selftest"

# -O2, not the suite's -Oz: this walks all 2^32 statuses twice, and -Oz is a
# size policy for the shipped image, not a property of the host test binary.
FLAGS=(-std=c23 -O2 -Wall -Wextra -Werror -Wconversion -Wshadow -Wcast-align
       -Wstrict-prototypes -Wmissing-prototypes -Wnull-dereference)

pass=0
fail=0
ok()  { pass=$((pass + 1)); printf 'PASS  %s\n' "$1"; }
bad() { fail=$((fail + 1)); printf 'FAIL  %s\n' "$1"; }
step() { printf '\n== %s ==\n' "$1"; }

# cmd's exit status, without set -e or a subshell swallowing it
rc=0
run() { rc=0; "$@" || rc=$?; }

rm -rf "$SCRATCH"
mkdir -p "$SCRATCH"
cp "$HERE/model.txt" "$SCRATCH/model.txt"
base_entries=$(grep -c '^0x' "$SCRATCH/model.txt")
real_before=$(sha256sum "$HERE/model.txt")

step "1: walk 1 — the model vs five planted XP answers"
if python3 "$HERE/gen_model.py" "$SCRATCH/model.txt" "$SCRATCH/table1.c" \
        >"$SCRATCH/gen1.log" 2>&1; then
    ok "unlearned model generates a table ($(tail -1 "$SCRATCH/gen1.log"))"
else
    bad "unlearned model generates a table"
    cat "$SCRATCH/gen1.log"
fi
if clang "${FLAGS[@]}" -DANEM_MV_NATIVE -I "$HERE" -o "$SCRATCH/walk1" \
        "$HERE/mv_probe.c" "$HERE/statusdos.c" "$SCRATCH/table1.c" \
        "$HERE/selftest_oracle.c"; then
    ok "native probe builds (ANEM_MV_NATIVE, real mv_probe.c)"
else
    bad "native probe builds (ANEM_MV_NATIVE, real mv_probe.c)"
fi
if [ -x "$SCRATCH/walk1" ]; then
    t0=$(date +%s)
    ANEM_MV_CERT="$SCRATCH/cert1.bin" "$SCRATCH/walk1"
    rc=$?
    if [ "$rc" -eq 3 ]; then
        ok "walk 1 found mismatches and reported exactly that (exit 3, $(( $(date +%s) - t0 ))s)"
    else
        bad "walk 1 exit code: $rc, expected 3 (mismatches found)"
    fi
    size=$(stat -c %s "$SCRATCH/cert1.bin" 2>/dev/null || echo missing)
    if [ "$size" = "$((16 + 5 * 12 + 56))" ]; then
        ok "certificate holds exactly 5 records ($size bytes)"
    else
        bad "certificate is $size bytes, expected $((16 + 5 * 12 + 56))"
    fi
fi

step "2: check_cert.py refuses to bless a divergent walk"
rc=0
python3 "$HERE/check_cert.py" "$SCRATCH/model.txt" "$SCRATCH/cert1.bin" \
    >"$SCRATCH/check1.log" 2>&1 || rc=$?
if grep -q 'DIVERGENT: 5 mismatches' "$SCRATCH/check1.log"; then
    ok "checker reports DIVERGENT with all five mismatches"
else
    bad "checker reports DIVERGENT with all five mismatches"
    cat "$SCRATCH/check1.log"
fi
if [ "$rc" -ne 0 ]; then
    ok "DIVERGENT is a non-zero exit (a green run needs no mismatches)"
else
    bad "DIVERGENT is a non-zero exit (got $rc)"
fi

step "3: learn.py dry run changes nothing, then --write folds all five in"
before=$(sha256sum "$SCRATCH/model.txt")
rc=0
python3 "$HERE/learn.py" "$SCRATCH/model.txt" "$SCRATCH/cert1.bin" \
    >"$SCRATCH/learn_dry.log" 2>&1 || rc=$?
if [ "$rc" -eq 0 ] && grep -q '5 entry change(s)' "$SCRATCH/learn_dry.log"; then
    ok "dry run reports 5 entry changes"
else
    bad "dry run reports 5 entry changes (exit $rc)"
    cat "$SCRATCH/learn_dry.log"
fi
if [ "$before" = "$(sha256sum "$SCRATCH/model.txt")" ]; then
    ok "dry run left model.txt byte-identical"
else
    bad "dry run left model.txt byte-identical — it wrote without --write"
fi
run python3 "$HERE/learn.py" "$SCRATCH/model.txt" "$SCRATCH/cert1.bin" --write
n=$(grep -c '^0x' "$SCRATCH/model.txt")
# A planted status can already be a learned entry in the shipped model (the
# 0x80000000 range belongs to XP, and the walk fills it), and then its
# correction *replaces* that entry instead of adding one — so the count grows
# by the planted statuses that are new, not by five.
added=0
for planted in 0x00000500 0x40000005 0x80000005 0xC0000200 0xC0000201; do
    grep -qi "^$planted " "$HERE/model.txt" || added=$((added + 1))
done
want=$((base_entries + added))
if [ "$rc" -eq 0 ] && [ "$n" -eq "$want" ]; then
    ok "learned model has $n entries ($base_entries + $added planted,"\
"$((5 - added)) replacing a learned entry)"
else
    bad "learned model: exit $rc, $n entries, expected $want"
fi

step "4: the learned model regenerates and the CRC moves"
run python3 "$HERE/gen_model.py" "$SCRATCH/model.txt" "$SCRATCH/table2.c"
if [ "$rc" -eq 0 ]; then
    ok "gen_model.py accepts the learned model"
else
    bad "gen_model.py accepts the learned model (out of order? redundant?)"
fi
if [ -f "$SCRATCH/table1.c.crc" ] && [ -f "$SCRATCH/table2.c.crc" ] \
        && [ "$(cat "$SCRATCH/table1.c.crc")" != "$(cat "$SCRATCH/table2.c.crc")" ]; then
    ok "table CRC changed: $(cat "$SCRATCH/table1.c.crc") -> $(cat "$SCRATCH/table2.c.crc")"
else
    bad "table CRC changed after learning"
fi

step "5: walk 2 — the learned model must match everywhere"
if clang "${FLAGS[@]}" -DANEM_MV_NATIVE -I "$HERE" -o "$SCRATCH/walk2" \
        "$HERE/mv_probe.c" "$HERE/statusdos.c" "$SCRATCH/table2.c" \
        "$HERE/selftest_oracle.c"; then
    ok "native probe builds against the learned table"
else
    bad "native probe builds against the learned table"
fi
if [ -x "$SCRATCH/walk2" ]; then
    ANEM_MV_CERT="$SCRATCH/cert2.bin" "$SCRATCH/walk2"
    rc=$?
    if [ "$rc" -eq 0 ]; then
        ok "walk 2 found zero mismatches (exit 0)"
    else
        bad "walk 2 exit code: $rc, expected 0 (converged)"
    fi
    size=$(stat -c %s "$SCRATCH/cert2.bin" 2>/dev/null || echo missing)
    if [ "$size" = "$((16 + 0 + 56))" ]; then
        ok "VERIFIED certificate is $size bytes (the size run.sh commits)"
    else
        bad "certificate is $size bytes, expected $((16 + 56))"
    fi
    run python3 "$HERE/check_cert.py" "$SCRATCH/model.txt" "$SCRATCH/cert2.bin"
    if [ "$rc" -eq 0 ]; then
        ok "check_cert.py says VERIFIED"
    else
        bad "check_cert.py says VERIFIED (exit $rc)"
        python3 "$HERE/check_cert.py" "$SCRATCH/model.txt" "$SCRATCH/cert2.bin"
    fi
fi

step "6: negative control — neither certificate verifies against the other model"
# cert1 judged a 17-entry table, the learned model has 22.
rc=0
python3 "$HERE/check_cert.py" "$SCRATCH/model.txt" "$SCRATCH/cert1.bin" \
    >"$SCRATCH/neg1.log" 2>&1 || rc=$?
if [ "$rc" -ne 0 ] && grep -q 'REFUSED' "$SCRATCH/neg1.log"; then
    ok "walk-1 certificate is REFUSED against the learned model"
else
    bad "walk-1 certificate is REFUSED against the learned model (exit $rc)"
    cat "$SCRATCH/neg1.log"
fi
# cert2 judged 22 entries; the real model.txt still has 17.
rc=0
python3 "$HERE/check_cert.py" "$HERE/model.txt" "$SCRATCH/cert2.bin" \
    >"$SCRATCH/neg2.log" 2>&1 || rc=$?
if [ "$rc" -ne 0 ] && grep -q 'REFUSED' "$SCRATCH/neg2.log"; then
    ok "walk-2 certificate is REFUSED against the shipped model"
else
    bad "walk-2 certificate is REFUSED against the shipped model (exit $rc)"
    cat "$SCRATCH/neg2.log"
fi
if [ "$real_before" = "$(sha256sum "$HERE/model.txt")" ]; then
    ok "the real model.txt was never modified by this test"
else
    bad "the real model.txt was never modified by this test"
fi

printf '\n%d passed, %d failed\n' "$pass" "$fail"
[ "$fail" -eq 0 ] || exit 1
exit 0
