#!/bin/bash
# run.sh — G1, the ABI compatibility proof.
#
# Question: do clang 23.1.3 and the tree's MSVC 13.00 agree on struct layout,
# calling convention and name decoration? Everything in P1+ is written by clang
# and called by MSVC-built code, so if the answer is no, every replacement
# silently corrupts structures.
#
# Four checks, each of which can fail:
#   A  layout      — same source, both compilers, both run, output diffed
#   B  decoration  — defined symbol names of both objects, compared
#   C  cross call  — clang callee + MSVC caller and the reverse, values checked
#   D  kernel lib  — clang object linked against real ntosrtl.lib (_RtlInitString@8)
# plus a negative control on the diff step, because a check that cannot fail
# is not a check.
#
# usage: tools/abi/run.sh        (exit 0 = G1 passes)
set -uo pipefail

ROOT=$(cd "$(dirname "$0")/../.." && pwd)
TOOLS="$ROOT/tools/abi"
OUT="$TOOLS/out"
LLVM="$HOME/.local/opt/llvm-mingw-20261006-msvcrt-ubuntu-22.04-x86_64/bin"

export PATH="$LLVM:$PATH"
export WINEDEBUG=-all WINEPREFIX=${WINEPREFIX:-$HOME/.wine-xp}

CLANG_FLAGS=(--target=i686-windows-msvc -fms-extensions -std=c23 -O2
             -fno-stack-protector -Wall -Wextra -c)

pass=0
fail=0
ok()   { pass=$((pass + 1)); printf 'PASS  %s\n' "$1"; }
bad()  { fail=$((fail + 1)); printf 'FAIL  %s\n' "$1"; }
step() { printf '\n== %s ==\n' "$1"; }

wincmd() {
    wine cmd /c "$(winepath -w "$TOOLS/wincmd.cmd")" "$(winepath -w "$TOOLS")" "$1" 2>&1 | tr -d '\r'
}

[ -d "$WINEPREFIX" ] || wineboot -i >/dev/null 2>&1
ln -sfn "$ROOT/Source/XPSP1" "$WINEPREFIX/dosdevices/w:"
mkdir -p "$OUT"

# ---- clang side -------------------------------------------------------------
step "A: clang compiles layout, decoration, callee, main, kernel probe"
for src in layout_probe decor_probe cross_callee cross_main kernellink_probe; do
    if clang "${CLANG_FLAGS[@]}" -I"$TOOLS" "$TOOLS/$src.c" -o "$OUT/${src}_clang.obj"; then
        ok "clang -c $src.c"
    else
        bad "clang -c $src.c"
    fi
done
[ "$fail" -eq 0 ] || { echo "clang objects failed; stopping"; exit 1; }

# ---- MSVC side + links ------------------------------------------------------
step "MSVC 13.00 compiles its four objects"
if wincmd mk_msvc.cmd | tee "$OUT/mk_msvc.log" | grep -q MSVC_OBJS_OK; then
    ok "cl 13.00 objects"
else
    bad "cl 13.00 objects"; tail -20 "$OUT/mk_msvc.log"; exit 1
fi

step "tree's link.exe links the mixed executables"
if wincmd mk_link.cmd | tee "$OUT/mk_link.log" | grep -q LINK_OK; then
    ok "mixed links"
else
    bad "mixed links"; tail -30 "$OUT/mk_link.log"; exit 1
fi

# ---- check A ----------------------------------------------------------------
step "A: struct layout — run both, diff"
wine "$OUT/layout_msvc.exe"  > "$OUT/layout_msvc.txt"  2>/dev/null
wine "$OUT/layout_clang.exe" > "$OUT/layout_clang.txt" 2>/dev/null
if [ -s "$OUT/layout_msvc.txt" ] && [ -s "$OUT/layout_clang.txt" ]; then
    ok "both layout probes produced output ($(wc -l < "$OUT/layout_clang.txt") lines)"
else
    bad "layout probe produced no output"
fi
if diff -u "$OUT/layout_msvc.txt" "$OUT/layout_clang.txt" > "$OUT/layout.diff"; then
    ok "layout identical"
else
    bad "layout differs"; cat "$OUT/layout.diff"
fi

# negative control: perturb one number and demand the diff catches it
sed 's/^sizeof abi_t_simple .*/sizeof abi_t_simple          999/' \
    "$OUT/layout_clang.txt" > "$OUT/layout_perturbed.txt"
if diff -q "$OUT/layout_msvc.txt" "$OUT/layout_perturbed.txt" >/dev/null; then
    bad "negative control: diff missed a perturbed layout"
else
    ok "negative control: diff catches a perturbed layout"
fi

# ---- check B ----------------------------------------------------------------
step "B: symbol decoration — compare defined symbols"
nm_list() { llvm-nm --defined-only "$1" | awk '{print $NF}' | grep -E '^(@|[A-Za-z_])' | grep -E 'abi_deco_|AbiKernelProbe' | sort; }
nm_list "$OUT/decor_msvc.obj"  > "$OUT/decor_msvc.sym"
nm_list "$OUT/decor_probe_clang.obj" > "$OUT/decor_clang.sym"
if [ -s "$OUT/decor_msvc.sym" ] && [ -s "$OUT/decor_clang.sym" ]; then
    ok "both objects define the probed symbols ($(wc -l < "$OUT/decor_clang.sym") each)"
else
    bad "a decoration object defines no probed symbols"
fi
if diff -u "$OUT/decor_msvc.sym" "$OUT/decor_clang.sym" > "$OUT/decor.diff"; then
    ok "decoration identical"
else
    bad "decoration differs"; cat "$OUT/decor.diff"
fi

# ---- check C ----------------------------------------------------------------
step "C: cross calls — both directions"
if wine "$OUT/cross_msvc_caller.exe" > "$OUT/cross_msvc_caller.txt" 2>&1; then
    ok "clang callee called by MSVC caller"
else
    bad "clang callee called by MSVC caller"; cat "$OUT/cross_msvc_caller.txt"
fi
if wine "$OUT/cross_clang_caller.exe" > "$OUT/cross_clang_caller.txt" 2>&1; then
    ok "MSVC callee called by clang caller"
else
    bad "MSVC callee called by clang caller"; cat "$OUT/cross_clang_caller.txt"
fi
grep -h "CROSS:" "$OUT"/cross_*_caller.txt

# ---- check D ----------------------------------------------------------------
step "D: clang object against real XP RTL library"
if wincmd mk_kernellink.cmd | tee "$OUT/mk_kernellink.log" | grep -q KERNELLINK_OK; then
    ok "kernellink.sys links against ntosrtl.lib"
    ls -la "$OUT/kernellink.sys"
else
    bad "kernel link"; tail -20 "$OUT/mk_kernellink.log"
fi

# ---- summary ----------------------------------------------------------------
printf '\n%d passed, %d failed\n' "$pass" "$fail"
if [ "$fail" -eq 0 ]; then
    echo "G1 ABI PROOF: PASS"
    exit 0
fi
echo "G1 ABI PROOF: FAIL"
exit 1
