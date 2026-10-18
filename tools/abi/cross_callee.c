/* cross_callee.c — G1 ABI proof, check C: cross-toolchain calls.
 *
 * Built by clang 23.1.3 and linked into an executable whose caller is built by
 * the tree's MSVC 13.00 — and vice versa. Every function is __stdcall, which on
 * x86 means: the callee cleans the stack, and the linker sees a decoration that
 * encodes the argument byte count. A layout or convention disagreement shows up
 * as a link error or as wrong values here, never silently.
 */
#include "abi_types.h"

static const unsigned char abi_bytes_in[7] = { 0, 3, 6, 9, 12, 15, 18 };

abi_cross_v12 __stdcall AbiCrossSum12(abi_cross_v12 a, abi_cross_v12 b, unsigned n)
{
    abi_cross_v12 r;
    r.lo  = a.lo + b.lo + n;
    r.hi  = a.hi ^ b.hi ^ n;
    r.aux = (unsigned)sizeof(abi_cross_v12);
    return r;
}

abi_cross_v8 __stdcall AbiCrossMul8(abi_cross_v8 a, abi_cross_v8 b)
{
    abi_cross_v8 r;
    r.v = a.v * b.v;
    return r;
}

int __stdcall AbiCrossProbe(const abi_cross_packed *in, abi_cross_out *out, int depth)
{
    abi_cross_v12 a, b;
    abi_cross_v8  x, y;
    unsigned      i;

    if (in == 0 || out == 0) {
        return -1;
    }
    /* read the caller's memory through the callee's own layout: any offset
     * disagreement reads the wrong bytes and fails the checks below. */
    if (in->tag != 'A')                        return 1;
    if (in->magic != ABI_CROSS_MAGIC)          return 2;
    if (in->unit != 0x1234)                    return 3;
    if (in->big != 0x1122334455667788i64)      return 4;
    for (i = 0; i < 7; i++) {
        if (in->bytes[i] != abi_bytes_in[i])   return 5;
    }
    if (in->ratio != 1.5)                      return 6;

    for (i = 0; i < 8; i++) {
        out->name[i] = 0;
    }
    out->name[0] = 'A'; out->name[1] = 'N'; out->name[2] = 'E';
    out->name[3] = 'M'; out->name[4] = 'O'; out->name[5] = 'S';
    out->packed_size = (unsigned)sizeof(abi_cross_packed);
    out->depth_check = (unsigned)depth ^ 0xA5u;

    a.lo = 1000; a.hi = 2000; a.aux = 3000;
    b.lo = 5;    b.hi = 6;    b.aux = 7;
    out->sum = AbiCrossSum12(a, b, (unsigned)depth);

    x.v = 0x100000000i64;
    y.v = 3;
    out->product = AbiCrossMul8(x, y);

    return 0;
}
