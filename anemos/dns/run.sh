#!/bin/bash
# P2: DNS (RFC 1035) — codec and client.
#
# Checks, each able to fail:
#   1  pinned clang builds the codec suite and the client scenario suite as
#      i686 PEs; both pass under Wine
#   2  weakened builds FAIL their suites: a lenient name reader and a
#      case-sensitive name comparison
#   3  differential corpora against two independent Python implementations:
#      - 1,365 valid/malformed/truncated/mutated codec cases;
#      - DNS client query/response cases (A, AAAA, CNAME chains, loops,
#        rcodes, wrong ids/questions, truncations), under ASan/UBSan
#   4  both units compile freestanding for i686-windows-msvc and stay inside
#      the 8 KB per-TU .text budget
#
# usage: anemos/dns/run.sh        (exit 0 = this unit passes)
set -uo pipefail

HERE=$(cd "$(dirname "$0")" && pwd)
OUT="$HERE/out"
LLVM="$HOME/.local/opt/llvm-mingw-20261006-msvcrt-ubuntu-22.04-x86_64/bin"

export PATH="$LLVM:$PATH"
export WINEDEBUG=-all WINEPREFIX=${WINEPREFIX:-$HOME/.wine-xp}

FLAGS=(-std=c23 -Oz -Wall -Wextra -Werror -Wconversion -Wshadow -Wcast-align
       -Wstrict-prototypes -Wmissing-prototypes -Wnull-dereference)

pass=0
fail=0
ok()  { pass=$((pass + 1)); printf 'PASS  %s\n' "$1"; }
bad() { fail=$((fail + 1)); printf 'FAIL  %s\n' "$1"; }
step() { printf '\n== %s ==\n' "$1"; }

[ -d "$WINEPREFIX" ] || wineboot -i >/dev/null 2>&1
mkdir -p "$OUT"

step "1: vector suites (pinned clang, i686 PE, Wine)"
if clang --target=i686-w64-mingw32 "${FLAGS[@]}" \
        -o "$OUT/dnstest.exe" "$HERE/dns.c" "$HERE/test_dns.c"; then
    ok "clang builds the codec suite"
    if wine "$OUT/dnstest.exe" 2>&1 | tr -d '\r' | tee "$OUT/dnstest.log"; then
        ok "codec suite passes"
    else
        bad "codec suite passes"
    fi
else
    bad "clang builds the codec suite"
fi

if clang --target=i686-w64-mingw32 "${FLAGS[@]}" \
        -o "$OUT/dnsclienttest.exe" "$HERE/dns.c" "$HERE/dns_client.c" \
        "$HERE/test_dns_client.c"; then
    ok "clang builds the client suite"
    if wine "$OUT/dnsclienttest.exe" 2>&1 | tr -d '\r' \
            | tee "$OUT/dnsclienttest.log"; then
        ok "client scenario suite passes"
    else
        bad "client scenario suite passes"
    fi
else
    bad "clang builds the client suite"
fi

step "2: negative controls — weakened builds must fail"
if clang --target=i686-w64-mingw32 "${FLAGS[@]}" -DANEM_DNS_NEGATIVE \
        -o "$OUT/dnsneg.exe" "$HERE/dns.c" "$HERE/test_dns.c"; then
    ok "weakened codec compiles"
    if wine "$OUT/dnsneg.exe" >/dev/null 2>&1; then
        bad "weakened codec is rejected"
    else
        ok "weakened codec is rejected"
    fi
else
    bad "weakened codec compiles"
fi

if clang --target=i686-w64-mingw32 "${FLAGS[@]}" -DANEM_DNS_CLIENT_NEGATIVE \
        -o "$OUT/dnsclientneg.exe" "$HERE/dns.c" "$HERE/dns_client.c" \
        "$HERE/test_dns_client.c"; then
    ok "weakened client compiles"
    if wine "$OUT/dnsclientneg.exe" >/dev/null 2>&1; then
        bad "weakened client is rejected"
    else
        ok "weakened client is rejected"
    fi
else
    bad "weakened client compiles"
fi

step "3: differential corpora against independent Python implementations"
if python3 "$HERE/dns_ref.py" --check >"$OUT/ref.log" 2>&1; then
    ok "codec reference self-check"
else
    bad "codec reference self-check"
    sed 's/^/    /' "$OUT/ref.log"
fi

if python3 "$HERE/dns_ref.py" --emit "$OUT" >"$OUT/emit.log" 2>&1; then
    ok "codec corpus generated"
    if /usr/bin/clang -std=c2x -O1 -g -fsanitize=address,undefined \
            -fno-omit-frame-pointer -Wall -Wextra -Werror \
            -o "$OUT/dnscorpus" "$HERE/dns.c" "$HERE/corpus_main.c"; then
        ok "native sanitised codec driver builds"
        if "$OUT/dnscorpus" "$OUT/corpus.bin" >"$OUT/got.txt" 2>"$OUT/corpus.err"; then
            ok "codec corpus driver ran"
            if diff -u "$OUT/expected.txt" "$OUT/got.txt" >"$OUT/diff.txt"; then
                ok "C and Python agree on every codec case"
            else
                bad "C and Python agree on every codec case"
                head -40 "$OUT/diff.txt" | sed 's/^/    /'
            fi
        else
            bad "codec corpus driver ran"
            sed 's/^/    /' "$OUT/corpus.err"
        fi
        if /usr/bin/clang -std=c2x -O1 -g -DANEM_DNS_NEGATIVE \
                -Wall -Wextra -Werror \
                -o "$OUT/dnscorpusneg" "$HERE/dns.c" "$HERE/corpus_main.c"; then
            if "$OUT/dnscorpusneg" "$OUT/corpus.bin" >"$OUT/gotneg.txt" 2>/dev/null \
               && ! diff -q "$OUT/expected.txt" "$OUT/gotneg.txt" >/dev/null; then
                ok "weakened codec is rejected by the corpus"
            else
                bad "weakened codec is rejected by the corpus"
            fi
        else
            bad "weakened codec corpus driver builds"
        fi
    else
        bad "native sanitised codec driver builds"
    fi
else
    bad "codec corpus generated"
    sed 's/^/    /' "$OUT/emit.log"
fi

if python3 "$HERE/dns_client_ref.py" --check >"$OUT/clientref.log" 2>&1; then
    ok "client reference self-check"
else
    bad "client reference self-check"
    sed 's/^/    /' "$OUT/clientref.log"
fi

if python3 "$HERE/dns_client_ref.py" --emit "$OUT/client" \
        >"$OUT/clientemit.log" 2>&1; then
    ok "client corpus generated"
    if /usr/bin/clang -std=c2x -O1 -g -fsanitize=address,undefined \
            -fno-omit-frame-pointer -Wall -Wextra -Werror \
            -o "$OUT/dnsclientdrv" "$HERE/dns.c" "$HERE/dns_client.c" \
            "$HERE/dns_client_main.c"; then
        ok "native sanitised client driver builds"
        if "$OUT/dnsclientdrv" "$OUT/client/corpus.bin" \
                >"$OUT/client_got.txt" 2>"$OUT/client.err"; then
            ok "client corpus driver ran"
            if diff -u "$OUT/client/expected.txt" "$OUT/client_got.txt" \
                    >"$OUT/clientdiff.txt"; then
                ok "C and Python agree on every client case"
            else
                bad "C and Python agree on every client case"
                head -40 "$OUT/clientdiff.txt" | sed 's/^/    /'
            fi
        else
            bad "client corpus driver ran"
            sed 's/^/    /' "$OUT/client.err"
        fi
        if /usr/bin/clang -std=c2x -O1 -g -DANEM_DNS_CLIENT_NEGATIVE \
                -Wall -Wextra -Werror \
                -o "$OUT/dnsclientdrvneg" "$HERE/dns.c" "$HERE/dns_client.c" \
                "$HERE/dns_client_main.c"; then
            if "$OUT/dnsclientdrvneg" "$OUT/client/corpus.bin" \
                    >"$OUT/client_neg.txt" 2>/dev/null \
               && ! diff -q "$OUT/client/expected.txt" "$OUT/client_neg.txt" \
                    >/dev/null; then
                ok "weakened client is rejected by the corpus"
            else
                bad "weakened client is rejected by the corpus"
            fi
        else
            bad "weakened client driver builds"
        fi
    else
        bad "native sanitised client driver builds"
    fi
else
    bad "client corpus generated"
    sed 's/^/    /' "$OUT/clientemit.log"
fi

step "4: freestanding i686-windows-msvc objects + size budgets"
if clang --target=i686-windows-msvc -ffreestanding -fno-builtin \
        -fno-stack-protector "${FLAGS[@]}" \
        -c "$HERE/dns.c" -o "$OUT/dns.i686.obj"; then
    ok "dns.c is freestanding-clean for i686-windows-msvc"
    hex=$(llvm-objdump -h "$OUT/dns.i686.obj" 2>/dev/null \
           | awk '$2 == ".text" { print $3; exit }')
    text=$(printf '%d' "0x$hex" 2>/dev/null)
    if [ -z "${hex:-}" ] || [ -z "${text:-}" ]; then
        bad "dns.c .text size readable"
    elif [ "$text" -le 8192 ]; then
        ok "dns.c .text is ${text} bytes (budget 8192)"
    else
        bad "dns.c .text is ${text} bytes (budget 8192)"
    fi
else
    bad "dns.c is freestanding-clean for i686-windows-msvc"
fi

if clang --target=i686-windows-msvc -ffreestanding -fno-builtin \
        -fno-stack-protector "${FLAGS[@]}" \
        -c "$HERE/dns_client.c" -o "$OUT/dns_client.i686.obj"; then
    ok "dns_client.c is freestanding-clean for i686-windows-msvc"
    hex=$(llvm-objdump -h "$OUT/dns_client.i686.obj" 2>/dev/null \
           | awk '$2 == ".text" { print $3; exit }')
    text=$(printf '%d' "0x$hex" 2>/dev/null)
    if [ -z "${hex:-}" ] || [ -z "${text:-}" ]; then
        bad "dns_client.c .text size readable"
    elif [ "$text" -le 8192 ]; then
        ok "dns_client.c .text is ${text} bytes (budget 8192)"
    else
        bad "dns_client.c .text is ${text} bytes (budget 8192)"
    fi
else
    bad "dns_client.c is freestanding-clean for i686-windows-msvc"
fi

printf '\n%d passed, %d failed\n' "$pass" "$fail"
[ "$fail" -eq 0 ] || exit 1
exit 0
