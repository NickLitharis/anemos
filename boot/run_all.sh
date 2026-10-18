#!/bin/sh
# Build and run every in-guest test, print PASS/FAIL, exit non-zero on failure.
CDIR=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$CDIR/.." && pwd)
fail=0
n=0
for f in "$ROOT"/kernel/tests/*.c; do
    t=$(basename "$f" .c)
    n=$((n + 1))
    ( cd "$CDIR" && ./build.sh ./out "$f" >/dev/null 2>&1 ) || { echo "FAIL $t (build)"; fail=$((fail + 1)); continue; }
    timeout 20 qemu-system-i386 -kernel "$CDIR/out/xp-test.elf" -display none \
            -debugcon file:"$CDIR/out/$t.txt" 2>/dev/null
    if tail -1 "$CDIR/out/$t.txt" 2>/dev/null | grep -q VERIFIED; then
        echo "PASS $t"
    else
        echo "FAIL $t"
        fail=$((fail + 1))
    fi
done
echo "---"
echo "$((n - fail))/$n passed"
[ "$fail" -eq 0 ] || exit 1
