#!/usr/bin/env bash
# P1: STATUS->DOS map — Anemos's table for ntdll!RtlNtStatusToDosError,
# extracted clean-room from XP's behaviour (sdos_probe.c ran in the guest and
# recorded what the shipped export returned for all 2^32 statuses).
#
# sdos.bin is that recording, committed as this unit's golden; table.c is
# regenerated from it on every run, so the table can never drift from the
# evidence. Five checks, each of which can fail:
#   1  gen_table.py must ACCEPT the dump (full tiling, published anchors)
#   2  the boundary suite (pinned clang, i686 PE, Wine) must pass
#   3  the same suite with the binary-search off-by-one (-DANEM_STATUSDOS_*
#      NEGATIVE) must FAIL
#   4  verify: all 2^32 statuses compared against the dump, natively
#   5  the negative verify must FAIL (exits at the first wrong status)
#   6  statusdos.c compiles warning-free freestanding for i686-windows-msvc
#
# usage: anemos/statusdos/run.sh [path/to/sdos.bin]     (exit 0 = unit passes)
set -uo pipefail

HERE=$(cd "$(dirname "$0")" && pwd)
OUT="$HERE/out"
LLVM="$HOME/.local/opt/llvm-mingw-20261006-msvcrt-ubuntu-22.04-x86_64/bin"
DUMP="${1:-$HERE/sdos.bin}"

export PATH="$LLVM:$PATH"
export WINEDEBUG=-all WINEPREFIX=${WINEPREFIX:-$HOME/.wine-xp}

FLAGS=(-std=c23 -Oz -Wall -Wextra -Werror -Wconversion -Wshadow -Wcast-align
       -Wstrict-prototypes -Wmissing-prototypes -Wnull-dereference)

pass=0
fail=0
ok()  { pass=$((pass + 1)); printf 'PASS  %s\n' "$1"; }
bad() { fail=$((fail + 1)); printf 'FAIL  %s\n' "$1"; }
step() { printf '\n== %s ==\n' "$1"; }

mkdir -p "$OUT"

step "1: gen_table.py accepts the dump (tiling + published anchors)"
# Fatal on failure: every later step must run against a table generated
# from the dump being tested, never a stale one left in out/.
if python3 "$HERE/gen_table.py" "$DUMP" "$OUT/table.c" | tee "$OUT/gen.log"; then
    ok "dump is complete and XP-consistent"
else
    bad "dump is complete and XP-consistent"
    printf '\n%d passed, %d failed\n' "$pass" "$fail"
    exit 1
fi

step "2: boundary + anchor suite (pinned clang, i686 PE, Wine)"
# Windows accepts '/' in paths, so the POSIX path rides straight through.
if clang --target=i686-w64-mingw32 "${FLAGS[@]}" -I "$HERE" \
        -o "$OUT/sdostest.exe" "$HERE/statusdos.c" "$OUT/table.c" \
        "$HERE/test_statusdos.c"; then
    ok "clang builds the suite"
    if wine "$OUT/sdostest.exe" "Z:$DUMP" 2>&1 | tr -d '\r' \
            | tee "$OUT/sdostest.log"; then
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
    if wine "$OUT/sdosneg.exe" "Z:$DUMP" >/dev/null 2>&1; then
        bad "off-by-one build is rejected"
    else
        ok "off-by-one build is rejected"
    fi
else
    bad "off-by-one build compiles"
fi

step "4: exhaustive differential — all 2^32 statuses vs the dump (native)"
if clang "${FLAGS[@]}" -I "$HERE" -o "$OUT/verify" \
        "$HERE/statusdos.c" "$OUT/table.c" "$HERE/verify.c"; then
    ok "native verify builds"
    if "$OUT/verify" "$DUMP" | tee "$OUT/verify.log"; then
        ok "every status matches XP"
    else
        bad "every status matches XP"
    fi
else
    bad "native verify builds"
fi

step "5: negative control — the exhaustive check must fail"
if clang "${FLAGS[@]}" -DANEM_STATUSDOS_NEGATIVE -I "$HERE" \
        -o "$OUT/verifyneg" \
        "$HERE/statusdos.c" "$OUT/table.c" "$HERE/verify.c"; then
    ok "negative verify builds"
    if "$OUT/verifyneg" "$DUMP" >/dev/null 2>&1; then
        bad "negative verify is rejected"
    else
        ok "negative verify is rejected"
    fi
else
    bad "negative verify builds"
fi

step "6: freestanding i686-windows-msvc object (the destination ABI)"
if clang --target=i686-windows-msvc -ffreestanding -fno-builtin \
        -fno-stack-protector "${FLAGS[@]}" \
        -c "$HERE/statusdos.c" -o "$OUT/statusdos.i686.obj"; then
    ok "statusdos.c is freestanding-clean for i686-windows-msvc"
else
    bad "statusdos.c is freestanding-clean for i686-windows-msvc"
fi

printf '\n%d passed, %d failed\n' "$pass" "$fail"
[ "$fail" -eq 0 ] || exit 1
exit 0
