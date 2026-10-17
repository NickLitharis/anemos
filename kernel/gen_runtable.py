#!/usr/bin/env python3
"""Build the runtime STATUS->DOS error table from the tree's own data.

`Source/XPSP1/NT/base/ntos/rtl/generr.c` contains `LONG CodePairs[]`, the
authoritative status/DOS error pair list that the razzle build feeds to
generr.exe to emit RtlpRunTable at build time. We cannot run that tool, so we
emit the runtime table directly from the same data: identical pairs, identical
values, resolved through the tree's own ntstatus.h / winerror.h.
"""
import json, re, sys

TREE = '/home/marocos/winxp_source/Source/XPSP1/NT'
VALS = json.load(open('/tmp/ntvals.json'))
VALS.setdefault('NO_ERROR', '0')

src = open(f'{TREE}/base/ntos/rtl/generr.c', encoding='latin-1').read()
i = src.find('LONG CodePairs[] = {')
body = src[i:src.find('};', i)]
body = re.sub(r'//[^\n]*', '', body)          # strip line comments first
body = body[body.find('{') + 1:]
toks = [t.strip() for t in body.split(',') if t.strip()]

pairs = []
for k in range(0, len(toks) - 1, 2):
    st, er = toks[k], toks[k + 1]
    if st in ('LONG CodePairs[] = {', ''):
        continue
    pairs.append((st, er))

def val(name):
    if name.startswith('0x'):
        return name
    return VALS.get(name)

out = ['''/* kernel/src/runtable_gen.c
 *
 * GENERATED - do not edit. Produced by kernel/gen_runtable.py from
 * Source/XPSP1/NT/base/ntos/rtl/generr.c (LONG CodePairs[]) and the tree's
 * ntstatus.h / winerror.h. This is the same data the razzle build feeds to
 * generr.exe to emit the runtime status->DOS table.
 */
#include "xpkernel.h"

typedef struct _XP_RUN_ENTRY { unsigned long Status; unsigned long Error; } XP_RUN_ENTRY;

const XP_RUN_ENTRY XpRunPairs[] = {''']

rows, skipped = 0, []
for st, er in pairs:
    a, b = val(st), val(er)
    if a is None or b is None:
        skipped.append((st, er)); continue
    out.append(f'    {{ {a}, {b} }},')
    rows += 1
out.append(f'    {{ 0xffffffff, 0 }}\n}};\n')
out.append(f'const unsigned long XpRunPairCount = {rows};\n')

open('kernel/src/runtable_gen.c', 'w').write('\n'.join(out))
print(f'rows emitted: {rows}, skipped: {len(skipped)}')
for s in skipped[:5]:
    print('  skipped', s)
