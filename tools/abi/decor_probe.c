/* decor_probe.c — G1 ABI proof, check B: symbol decoration.
 *
 * Compiled by both compilers but never linked or run. The defined symbol names
 * of the two objects are compared with llvm-nm: __cdecl, __stdcall, __fastcall,
 * struct-by-value argument counts, struct return (hidden pointer) and plain
 * data. A mismatch here means the linker — and therefore the kernel — would not
 * find the replacement, or would call it with the wrong stack shape.
 */
#include "abi_types.h"

int abi_deco_cdecl(int a, char *b)
{
    return a + (b != 0 ? *b : 0);
}

int __stdcall abi_deco_stdcall(int a, char *b, __int64 c)
{
    return a + (b != 0 ? *b : 0) + (int)(c & 1);
}

int __fastcall abi_deco_fastcall(int a, int b)
{
    return a + b;
}

/* struct by value: the @N in the decoration is the stack byte count, so a
 * layout disagreement surfaces here even before anything runs. */
int __stdcall abi_deco_byval12(abi_cross_v12 a, int n)
{
    return (int)(a.lo + a.hi + a.aux) + n;
}

int __stdcall abi_deco_byval8(abi_cross_v8 a, int n)
{
    return (int)(a.v & 0xffff) + n;
}

/* packed struct behind a pointer: two pointers = @8 */
int __stdcall abi_deco_packed(const abi_cross_packed *p, int n)
{
    return (p != 0 ? p->magic : 0u) + n;
}

/* struct returned by value > 8 bytes: hidden pointer argument is counted too */
abi_cross_v12 __stdcall abi_deco_ret12(unsigned seed)
{
    abi_cross_v12 r;
    r.lo = seed;
    r.hi = seed ^ 0xffffffffu;
    r.aux = (unsigned)sizeof(abi_cross_v12);
    return r;
}

int abi_deco_global;
const char abi_deco_rodata[] = "abi-deco";
