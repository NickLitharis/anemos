#!/usr/bin/env bash
# P4.4: DNS resolver over the socket path.
#
# Checks, each able to fail:
#   1  the pinned clang builds the resolver trace harness
#   2  every generated trace produces identical glue output in C and in a
#      Python model that orchestrates the independent Python socket and
#      DNS-client models (both processes are bounded and their status checked)
#   3  the corpus actually reaches ok, dns and timeout outcomes
#   4  a negative build, compiled into the probe only, is rejected on every
#      trace that expects a successful lookup
#   5  the resolver stays inside the 8 KB per-TU .text budget
#
# Response frames are built once by the P2 Python client model and embedded as
# hex, so both sides inject identical bytes and the diff is about ordering,
# retries and status mapping, not DNS parsing (already gated by P2).
#
# usage: anemos/dns/resolver_run.sh
set -uo pipefail

HERE=$(cd "$(dirname "$0")" && pwd)
REPO=$(cd "$HERE/../.." && pwd)
OUT="$HERE/out/resolver"
TRACES="$OUT/traces"
LLVM="$HOME/.local/opt/llvm-mingw-20261006-msvcrt-ubuntu-22.04-x86_64/bin"

export PATH="$LLVM:$PATH"
export WINEDEBUG=-all WINEPREFIX=${WINEPREFIX:-$HOME/.wine-xp}

FLAGS=(-std=c23 -Oz -Wall -Wextra -Werror -Wconversion -Wshadow -Wcast-align
       -Wstrict-prototypes -Wmissing-prototypes -Wnull-dereference
       -I "$REPO/anemos/winsock" -I "$REPO/anemos/dns")
SOURCES=("$HERE/resolver.c" "$HERE/dns.c" "$HERE/dns_client.c"
         "$REPO/anemos/winsock/socket_core.c" "$REPO/anemos/winsock/addr.c"
         "$HERE/resolver_probe.c")

pass=0
fail=0
ok()  { pass=$((pass + 1)); printf 'PASS  %s\n' "$1"; }
bad() { fail=$((fail + 1)); printf 'FAIL  %s\n' "$1"; }
step() { printf '\n== %s ==\n' "$1"; }

[ -d "$WINEPREFIX" ] || wineboot -i >/dev/null 2>&1
mkdir -p "$OUT"

step "1: build the resolver harness"
if clang --target=i686-w64-mingw32 "${FLAGS[@]}" \
        -o "$OUT/resolver_probe.exe" "${SOURCES[@]}"; then
    ok "resolver harness builds"
else
    bad "resolver harness builds"
    printf '\n%d passed, %d failed\n' "$pass" "$fail"
    exit 1
fi

rm -rf "$TRACES"
if ! python3 "$HERE/gen_resolver_traces.py" "$TRACES"; then
    bad "trace generator runs"
    printf '\n%d passed, %d failed\n' "$pass" "$fail"
    exit 1
fi
ok "trace generator runs"
expected=$(python3 -c "import sys; sys.path.insert(0, '$HERE'); import gen_resolver_traces as g; print(len(g.cases()))")
trace_count=$(find "$TRACES" -name '*.txt' | wc -l)
if [ "$trace_count" -eq "$expected" ]; then
    ok "corpus has the expected $expected traces"
else
    bad "corpus has the expected $expected traces (found $trace_count)"
fi

step "2: differential traces against the independent model"
mismatches=0
for trace in "$TRACES"/*.txt; do
    name=$(basename "$trace" .txt)
    if ! timeout 60 python3 "$HERE/resolver_ref.py" "$trace" \
            >"$OUT/want-$name.txt" 2>"$OUT/err-$name.txt"; then
        mismatches=$((mismatches + 1))
        printf '  model failed: %s\n' "$name"
        continue
    fi
    if ! (cd "$OUT" && timeout 60 wine resolver_probe.exe) <"$trace" \
            2>/dev/null | tr -d '\r' >"$OUT/got-$name.txt"; then
        mismatches=$((mismatches + 1))
        printf '  harness failed: %s\n' "$name"
        continue
    fi
    if ! diff -u "$OUT/want-$name.txt" "$OUT/got-$name.txt" \
            >"$OUT/diff-$name.txt"; then
        mismatches=$((mismatches + 1))
        printf '  mismatch: %s\n' "$name"
        head -16 "$OUT/diff-$name.txt" | sed 's/^/    /'
    fi
done
if [ "$mismatches" -eq 0 ]; then
    ok "$trace_count traces match the independent model"
else
    bad "$trace_count traces match the independent model ($mismatches bad)"
fi

step "3: the corpus reaches every terminal outcome"
ok_traces=$(grep -l '^result ok' "$OUT"/want-*.txt 2>/dev/null | wc -l)
dns_traces=$(grep -l '^result dns' "$OUT"/want-*.txt 2>/dev/null | wc -l)
timeout_traces=$(grep -l '^result timeout' "$OUT"/want-*.txt 2>/dev/null | wc -l)
if [ "$ok_traces" -gt 0 ] && [ "$dns_traces" -gt 0 ] && [ "$timeout_traces" -gt 0 ]; then
    ok "outcomes covered: ok=$ok_traces dns=$dns_traces timeout=$timeout_traces"
else
    bad "outcomes covered: ok=$ok_traces dns=$dns_traces timeout=$timeout_traces"
fi

step "4: negative control — every successful lookup must flip"
if clang --target=i686-w64-mingw32 "${FLAGS[@]}" -DANEM_RESOLVER_NEGATIVE \
        -o "$OUT/resolver_probe_neg.exe" "${SOURCES[@]}"; then
    ok "negative harness builds"
    negative=0
    for trace in "$TRACES"/*.txt; do
        name=$(basename "$trace" .txt)
        if ! (cd "$OUT" && timeout 60 wine resolver_probe_neg.exe) <"$trace" \
                2>/dev/null | tr -d '\r' >"$OUT/ngot-$name.txt"; then
            negative=$((negative + 1))
            continue
        fi
        if ! diff -q "$OUT/want-$name.txt" "$OUT/ngot-$name.txt" \
                >/dev/null 2>&1; then
            negative=$((negative + 1))
        fi
    done
    if [ "$negative" -eq "$ok_traces" ]; then
        ok "negative build is rejected on all $ok_traces ok traces"
    else
        bad "negative build is rejected on all $ok_traces ok traces (got $negative)"
    fi
else
    bad "negative harness builds"
fi

step "5: size budget"
if clang --target=i686-w64-mingw32 "${FLAGS[@]}" -c \
        -o "$OUT/resolver.obj" "$HERE/resolver.c"; then
    text_hex=$(llvm-objdump -h "$OUT/resolver.obj" \
        | awk '$2 == ".text" { print $3 }')
    if [ -n "$text_hex" ]; then
        text=$((16#$text_hex))
        printf 'resolver.c .text: %s bytes\n' "$text"
        if [ "$text" -le 8192 ]; then
            ok "resolver.c fits the 8 KB .text budget"
        else
            bad "resolver.c fits the 8 KB .text budget"
        fi
    else
        bad "resolver.c fits the 8 KB .text budget (no .text section)"
    fi
else
    bad "resolver.c compiles to an object"
fi

printf '\n%d passed, %d failed\n' "$pass" "$fail"
[ "$fail" -eq 0 ] || exit 1
exit 0
