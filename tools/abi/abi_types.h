/* abi_types.h — G1 ABI proof: the shared declaration matrix.
 *
 * One header, included by translation units built by BOTH compilers:
 *   - clang 23.1.3, --target=i686-windows-msvc -fms-extensions -std=c23
 *   - the tree's MSVC 13.00 (Source/XPSP1/NT/tools/x86/cl.exe)
 *
 * Must stay C89-parseable: MSVC 13.00 predates C99. Every construct here is one
 * the replacement boundary will actually carry. If the two compilers disagree on
 * any sizeof/offsetof/symbol decoration below, G1 fails and nothing downstream
 * may be trusted.
 */
#ifndef ABI_TYPES_H
#define ABI_TYPES_H

/* offsetof without <stddef.h>: neither compiler may bring its own headers in,
 * so the two sides see byte-for-byte the same declarations. */
#if defined(_MSC_VER) && !defined(__clang__)
typedef unsigned long abi_size_t;
#define ABI_OFFSETOF(type, member) ((abi_size_t)&(((type *)0)->member))
#else
#define ABI_OFFSETOF(type, member) ((unsigned)__builtin_offsetof(type, member))
#endif

/* --- natural packing (MSVC default /Zp8; clang matches on the msvc target) --- */
#pragma pack(push, 8)
typedef struct { char c; int i; char c2; __int64 q; }                  abi_t_simple;
typedef struct { char c; double d; char c2; }                          abi_t_double;
typedef struct { short s; char c; }                                    abi_t_pad;
typedef struct { int a[3]; char tail; }                                abi_t_array;
typedef struct { void *p; void (*fp)(int); unsigned long ul; }         abi_t_ptr;
typedef struct { char c; struct { char b; int c2; } inner; }           abi_t_nested;
typedef union { char c[9]; int i; __int64 q; double d; }               abi_t_union;
typedef enum { ABI_E0, ABI_E1 = 5, ABI_E2 = 100 }                      abi_t_enum;
typedef struct { abi_t_enum e; char c; }                               abi_t_enum_struct;
typedef long double                                                    abi_t_ldouble;
typedef struct { unsigned f0:1; unsigned f1:3; unsigned f2:28; }       abi_t_bits_u;
typedef struct { unsigned char a:3; unsigned char b:5; char c:2; }     abi_t_bits_mixed;
typedef struct { int x:1; long y:31; }                                 abi_t_bits_long;
typedef struct { char c; union { int i; double d; } u; }               abi_t_struct_union;
typedef struct { __declspec(align(16)) char a[4]; char c; }            abi_t_aligned;
typedef unsigned (__stdcall *abi_t_fp_stdcall)(int, char *);
#pragma pack(pop)

/* --- explicit packing: the boundary must carry #pragma pack across TUs --- */
#pragma pack(push, 1)
typedef struct { char c; int i; __int64 q; double d; }                 abi_t_p1;
typedef struct {
    char       tag;
    int        magic;
    short      unit;
    __int64    big;
    unsigned char bytes[7];
    double     ratio;
} abi_cross_packed;
#pragma pack(pop)

#pragma pack(push, 4)
typedef struct { char c; __int64 q; double d; }                        abi_t_p4;
#pragma pack(pop)

/* --- by-value argument / return types for the cross-call tests --- */
typedef struct { unsigned lo; unsigned hi; unsigned aux; }             abi_cross_v12;
typedef struct { __int64 v; }                                          abi_cross_v8;

typedef struct {
    char           name[8];
    unsigned       packed_size;
    unsigned       depth_check;
    abi_cross_v12  sum;
    abi_cross_v8   product;
} abi_cross_out;

/* clang-built callee, called by an MSVC-built caller (and the reverse). */
int __stdcall            AbiCrossProbe(const abi_cross_packed *in, abi_cross_out *out, int depth);
abi_cross_v12 __stdcall  AbiCrossSum12(abi_cross_v12 a, abi_cross_v12 b, unsigned n);
abi_cross_v8  __stdcall  AbiCrossMul8(abi_cross_v8 a, abi_cross_v8 b);

/* Value every TU must agree on before any call happens. */
#define ABI_CROSS_MAGIC   0x5A5A1234u
#define ABI_CROSS_DEPTH   7

#endif /* ABI_TYPES_H */
