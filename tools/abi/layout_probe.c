/* layout_probe.c — G1 ABI proof, check A: struct layout.
 *
 * Identical source compiled by clang 23.1.3 and by the tree's MSVC 13.00, then
 * both executables are run and their output diffed. Any disagreement in a
 * sizeof or offsetof is an ABI break.
 *
 * No system headers: printf is declared by hand so that neither compiler's CRT
 * header set can influence what is being measured.
 */
#include "abi_types.h"

int printf(const char *fmt, ...);

#define ABI_SIZEOF(type) \
    printf("sizeof %-22s %u\n", #type, (unsigned)sizeof(type))
#define ABI_OFFSET(type, member) \
    printf("offset %-22s %-10s %u\n", #type, #member, (unsigned)ABI_OFFSETOF(type, member))

int main(void)
{
    ABI_SIZEOF(abi_t_simple);
    ABI_OFFSET(abi_t_simple, i);
    ABI_OFFSET(abi_t_simple, c2);
    ABI_OFFSET(abi_t_simple, q);

    ABI_SIZEOF(abi_t_double);
    ABI_OFFSET(abi_t_double, d);
    ABI_OFFSET(abi_t_double, c2);

    ABI_SIZEOF(abi_t_pad);
    ABI_OFFSET(abi_t_pad, c);

    ABI_SIZEOF(abi_t_array);
    ABI_OFFSET(abi_t_array, a);
    ABI_OFFSET(abi_t_array, tail);

    ABI_SIZEOF(abi_t_ptr);
    ABI_OFFSET(abi_t_ptr, fp);
    ABI_OFFSET(abi_t_ptr, ul);

    ABI_SIZEOF(abi_t_nested);
    ABI_OFFSET(abi_t_nested, inner);
    ABI_OFFSET(abi_t_nested, inner.b);

    ABI_SIZEOF(abi_t_union);
    ABI_OFFSET(abi_t_union, q);
    ABI_OFFSET(abi_t_union, d);

    ABI_SIZEOF(abi_t_enum);
    ABI_SIZEOF(abi_t_enum_struct);
    ABI_OFFSET(abi_t_enum_struct, c);

    ABI_SIZEOF(abi_t_ldouble);

    /* bit-fields have no address, so sizeof is the only portable measurement;
     * a placement disagreement changes the size, or the size of a struct that
     * embeds it, which the following lines catch. */
    ABI_SIZEOF(abi_t_bits_u);
    ABI_SIZEOF(abi_t_bits_mixed);
    ABI_SIZEOF(abi_t_bits_long);

    ABI_SIZEOF(abi_t_struct_union);
    ABI_OFFSET(abi_t_struct_union, u);

    ABI_SIZEOF(abi_t_aligned);
    ABI_OFFSET(abi_t_aligned, c);

    ABI_SIZEOF(abi_t_fp_stdcall);

    ABI_SIZEOF(abi_t_p1);
    ABI_OFFSET(abi_t_p1, i);
    ABI_OFFSET(abi_t_p1, q);
    ABI_OFFSET(abi_t_p1, d);

    ABI_SIZEOF(abi_t_p4);
    ABI_OFFSET(abi_t_p4, q);
    ABI_OFFSET(abi_t_p4, d);

    ABI_SIZEOF(abi_cross_packed);
    ABI_OFFSET(abi_cross_packed, magic);
    ABI_OFFSET(abi_cross_packed, unit);
    ABI_OFFSET(abi_cross_packed, big);
    ABI_OFFSET(abi_cross_packed, bytes);
    ABI_OFFSET(abi_cross_packed, ratio);

    ABI_SIZEOF(abi_cross_v12);
    ABI_OFFSET(abi_cross_v12, hi);
    ABI_OFFSET(abi_cross_v12, aux);

    ABI_SIZEOF(abi_cross_v8);
    ABI_OFFSET(abi_cross_v8, v);

    ABI_SIZEOF(abi_cross_out);
    ABI_OFFSET(abi_cross_out, packed_size);
    ABI_OFFSET(abi_cross_out, depth_check);
    ABI_OFFSET(abi_cross_out, sum);
    ABI_OFFSET(abi_cross_out, product);

    return 0;
}
