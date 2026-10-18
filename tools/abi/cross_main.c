/* cross_main.c — G1 ABI proof, check C: the other half of the cross-call.
 *
 * This TU is built by the compiler that did NOT build cross_callee.c. It fills
 * the argument struct with values chosen here, calls the foreign callee, then
 * checks every returned field against expectations computed locally. Any
 * disagreement prints FAIL and exits non-zero.
 */
#include "abi_types.h"

int printf(const char *fmt, ...);

static int abi_failures;

static void abi_check_u(const char *what, unsigned got, unsigned want)
{
    if (got != want) {
        printf("FAIL %-14s got=%u (0x%x) want=%u (0x%x)\n", what, got, got, want, want);
        abi_failures++;
    }
}

static void abi_check_i(const char *what, int got, int want)
{
    if (got != want) {
        printf("FAIL %-14s got=%d want=%d\n", what, got, want);
        abi_failures++;
    }
}

static void abi_check_name(const char *what, const char *got, const char *want)
{
    int i;
    for (i = 0; i < 8; i++) {
        if (got[i] != want[i]) {
            printf("FAIL %-14s byte %d got=0x%02x want=0x%02x\n",
                   what, i, (unsigned)(unsigned char)got[i], (unsigned)(unsigned char)want[i]);
            abi_failures++;
            return;
        }
    }
}

int main(void)
{
    abi_cross_packed in;
    abi_cross_out    out;
    abi_cross_v12    a, b, r;
    abi_cross_v8     x8, y8, p8;
    int              rc;
    unsigned         i;

    for (i = 0; i < sizeof(out); i++) {
        ((unsigned char *)&out)[i] = 0;
    }

    in.tag   = 'A';
    in.magic = ABI_CROSS_MAGIC;
    in.unit  = 0x1234;
    in.big   = 0x1122334455667788i64;
    for (i = 0; i < 7; i++) {
        in.bytes[i] = (unsigned char)(i * 3);
    }
    in.ratio = 1.5;

    rc = AbiCrossProbe(&in, &out, ABI_CROSS_DEPTH);
    abi_check_i("probe rc", rc, 0);
    abi_check_name("name", out.name, "ANEMOS\0\0");
    abi_check_u("packed_size", out.packed_size, (unsigned)sizeof(abi_cross_packed));
    abi_check_u("depth_check", out.depth_check, (unsigned)ABI_CROSS_DEPTH ^ 0xA5u);

    /* sum = callee's formula, computed here independently */
    a.lo = 1000; a.hi = 2000; a.aux = 3000;
    b.lo = 5;    b.hi = 6;    b.aux = 7;
    r.lo = a.lo + b.lo + ABI_CROSS_DEPTH;
    r.hi = a.hi ^ b.hi ^ (unsigned)ABI_CROSS_DEPTH;
    r.aux = (unsigned)sizeof(abi_cross_v12);
    abi_check_u("sum.lo",  out.sum.lo,  r.lo);
    abi_check_u("sum.hi",  out.sum.hi,  r.hi);
    abi_check_u("sum.aux", out.sum.aux, r.aux);

    abi_check_u("product", (unsigned)(out.product.v & 0xffffffffu), 0u);
    abi_check_u("product hi", (unsigned)(out.product.v >> 32), 3u);

    /* direct by-value calls, both arg-passing and struct return */
    a.lo = 10; a.hi = 20; a.aux = 30;
    b.lo = 1;  b.hi = 2;  b.aux = 3;
    r = AbiCrossSum12(a, b, 100);
    abi_check_u("sum2.lo",  r.lo,  111u);
    abi_check_u("sum2.hi",  r.hi,  (20u ^ 2u ^ 100u));
    abi_check_u("sum2.aux", r.aux, (unsigned)sizeof(abi_cross_v12));

    x8.v = 0x1122334455667788i64;
    y8.v = 3;
    p8 = AbiCrossMul8(x8, y8);
    abi_check_u("mul8 lo", (unsigned)(p8.v & 0xffffffffu),
                (unsigned)(0x55667788u * 3u));
    abi_check_u("mul8 hi", (unsigned)(p8.v >> 32),
                (unsigned)((0x1122334455667788i64 * 3) >> 32));

    if (abi_failures != 0) {
        printf("CROSS: %d check(s) FAILED\n", abi_failures);
        return 1;
    }
    printf("CROSS: all checks passed\n");
    return 0;
}
