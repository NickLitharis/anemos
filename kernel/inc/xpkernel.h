/* kernel/inc/xpkernel.h
 *
 * Kernel-mode environment for building XP sources freestanding (no razzle, no
 * MSVC, no DDK installation). Layered on xpntdef.h, which is derived from the
 * tree's own ntdef.h.
 *
 * What lives here: calling-convention macros, kernel annotation stubs, the
 * STATUS_ codes the RTL uses, and the few runtime hooks (allocator, printf)
 * that kernel sources assume exist.
 */
#ifndef _XPKERNEL_H_
#define _XPKERNEL_H_

#include "xpntdef.h"

typedef BOOLEAN *PBOOLEAN;

/* --- calling conventions and annotations ------------------------------- */
#ifndef NTAPI
#define NTAPI
#endif
#ifndef NTSYSAPI
#define NTSYSAPI
#endif
#ifndef NTKERNELAPI
#define NTKERNELAPI
#endif
#ifndef FASTCALL
#define FASTCALL __attribute__((fastcall))
#endif
#ifndef IN
#define IN
#endif
#ifndef OUT
#define OUT
#endif
#ifndef OPTIONAL
#define OPTIONAL
#endif
#ifndef CONST
#define CONST const
#endif
#ifndef VOID
#define VOID void
#endif
#ifndef RTL_PAGED_CODE
#define RTL_PAGED_CODE()
#endif
#ifndef PAGED_CODE
#define PAGED_CODE()
#endif

/* --- runtime hooks the kernel sources assume --------------------------- */
typedef __builtin_va_list va_list;
#define va_start(v, l) __builtin_va_start(v, l)
#define va_end(v)      __builtin_va_end(v)
#define va_arg(v, t)   __builtin_va_arg(v, t)
#define va_copy(d, s)  __builtin_va_copy(d, s)

#ifndef InterlockedCompareExchange
static __inline__ long InterlockedCompareExchange(volatile long *d, long x, long c) {
    long old;
    __asm__ __volatile__("lock cmpxchgl %2, %1" : "=a"(old), "+m"(*d) : "r"(x), "a"(c) : "memory");
    return old;
}
#endif
#ifndef InterlockedIncrement
static __inline__ long InterlockedIncrement(volatile long *d) {
    return __sync_add_and_fetch(d, 1);
}
#endif
#ifndef InterlockedDecrement
static __inline__ long InterlockedDecrement(volatile long *d) {
    return __sync_sub_and_fetch(d, 1);
}
#endif


/* pointer-sized integers (normally from basetsd.h) */
typedef unsigned long ULONG_PTR, *PULONG_PTR; typedef long LONG_PTR, *PLONG_PTR;
typedef unsigned long DWORD_PTR, UINT_PTR; typedef long INT_PTR;


/* LARGE_INTEGER helper inlines from ntrtl.h that the RTL sources call */
#ifndef ARGUMENT_PRESENT
#define ARGUMENT_PRESENT(p) ((p) != 0)
#endif
static __inline__ LARGE_INTEGER RtlExtendedLargeIntegerDivide(LARGE_INTEGER Dividend, ULONG Divisor, PULONG Remainder) {
    LARGE_INTEGER q;
    q.QuadPart = (ULONG64)Dividend.QuadPart / Divisor;
    if (ARGUMENT_PRESENT(Remainder)) *Remainder = (ULONG)(Dividend.QuadPart % Divisor);
    return q;
}
static __inline__ LARGE_INTEGER RtlLargeIntegerDivide(LARGE_INTEGER Dividend, LARGE_INTEGER Divisor, PLARGE_INTEGER Remainder) {
    LARGE_INTEGER q;
    q.QuadPart = Dividend.QuadPart / Divisor.QuadPart;
    if (ARGUMENT_PRESENT(Remainder)) Remainder->QuadPart = Dividend.QuadPart % Divisor.QuadPart;
    return q;
}

/* --- STATUS codes used by the RTL -------------------------------------- */
#define STATUS_SUCCESS                  ((NTSTATUS)0x00000000L)
#define STATUS_UNSUCCESSFUL             ((NTSTATUS)0xC0000001L)
#define STATUS_NOT_IMPLEMENTED          ((NTSTATUS)0xC0000002L)
#define STATUS_INVALID_INFO_CLASS       ((NTSTATUS)0xC0000003L)
#define STATUS_INFO_LENGTH_MISMATCH     ((NTSTATUS)0xC0000004L)
#define STATUS_INVALID_HANDLE           ((NTSTATUS)0xC0000008L)
#define STATUS_INVALID_PARAMETER        ((NTSTATUS)0xC000000DL)
#define STATUS_NO_MEMORY                ((NTSTATUS)0xC0000017L)
#define STATUS_BUFFER_TOO_SMALL         ((NTSTATUS)0xC0000023L)
#define STATUS_ACCESS_DENIED            ((NTSTATUS)0xC0000022L)
#define STATUS_OBJECT_NAME_INVALID      ((NTSTATUS)0xC0000033L)
#define STATUS_OBJECT_NAME_NOT_FOUND    ((NTSTATUS)0xC0000034L)
#define STATUS_OBJECT_NAME_COLLISION    ((NTSTATUS)0xC0000035L)
#define STATUS_OBJECT_PATH_NOT_FOUND    ((NTSTATUS)0xC000003AL)
#define STATUS_OBJECT_PATH_SYNTAX_BAD   ((NTSTATUS)0xC000003BL)
#define STATUS_NAME_TOO_LONG            ((NTSTATUS)0xC0000106L)
#define STATUS_NOT_SUPPORTED            ((NTSTATUS)0xC00000BBL)
#define STATUS_PRIVILEGE_NOT_HELD       ((NTSTATUS)0xC0000061L)
#define STATUS_BUFFER_OVERFLOW          ((NTSTATUS)0x80000005L)
#define STATUS_NO_MATCH               ((NTSTATUS)0xC0000272L)
#define STATUS_NO_MORE_MATCHES        ((NTSTATUS)0xC0000273L)
#define NT_SUCCESS(s)                   (((NTSTATUS)(s)) >= 0)

#define UNICODE_NULL        ((WCHAR)0)
#define UNICODE_STRING_MAX_CHARS 32767


/* --- generic-function types used by the RTL tables --------------------- */
typedef unsigned char CCHAR_T;
typedef BOOLEAN (*PRTL_GENERIC_COMPARE_ROUTINE)(void *Table, PVOID First, PVOID Second);
typedef PVOID   (*PRTL_GENERIC_ALLOCATE_ROUTINE)(void *Table, ULONG ByteSize);
typedef void    (*PRTL_GENERIC_FREE_ROUTINE)(void *Table, PVOID Buffer);
typedef void    *RTL_GENERIC_TABLE_PLACEHOLDER;


typedef struct _RTL_BITMAP { ULONG SizeOfBitMap; PULONG Buffer; } RTL_BITMAP, *PRTL_BITMAP;

/* layouts copied from the tree's public/sdk/inc/ntrtl.h */
typedef struct _RTL_SPLAY_LINKS { struct _RTL_SPLAY_LINKS *Parent, *LeftChild, *RightChild; } RTL_SPLAY_LINKS, *PRTL_SPLAY_LINKS;

typedef struct _PREFIX_TABLE_ENTRY {
    CSHORT NodeTypeCode;
    CSHORT NameLength;
    struct _PREFIX_TABLE_ENTRY *NextPrefixTree;
    RTL_SPLAY_LINKS Links;
    PSTRING Prefix;
} PREFIX_TABLE_ENTRY, *PPREFIX_TABLE_ENTRY;

typedef struct _PREFIX_TABLE {
    CSHORT NodeTypeCode;
    CSHORT NameLength;
    PREFIX_TABLE_ENTRY *NextPrefixTree;
} PREFIX_TABLE, *PPREFIX_TABLE;

typedef struct _RTL_BITMAP_EX { ULONGLONG SizeOfBitMap; unsigned long long *Buffer; } RTL_BITMAP_EX;

typedef struct _RTL_BALANCED_LINKS {
    struct _RTL_BALANCED_LINKS *Parent, *LeftChild, *RightChild;
    char Balance; unsigned char Reserved[3];
} RTL_BALANCED_LINKS, *PRTL_BALANCED_LINKS;


typedef enum _RTL_GENERIC_COMPARE_RESULTS {
    GenericLessThan, GenericGreaterThan, GenericEqual
} RTL_GENERIC_COMPARE_RESULTS;
typedef RTL_GENERIC_COMPARE_RESULTS (*PRTL_AVL_COMPARE_ROUTINE)(struct _RTL_AVL_TABLE *, void *, void *);
typedef void *(*PRTL_AVL_ALLOCATE_ROUTINE)(struct _RTL_AVL_TABLE *, unsigned long);
typedef void (*PRTL_AVL_FREE_ROUTINE)(struct _RTL_AVL_TABLE *, void *);

typedef RTL_GENERIC_COMPARE_RESULTS (*PRTL_AVL_MATCH_FUNCTION)(struct _RTL_AVL_TABLE *, void *, void *);
typedef struct _RTL_AVL_TABLE {
    RTL_BALANCED_LINKS BalancedRoot;
    PVOID OrderedPointer;
    ULONG WhichOrderedElement;
    ULONG NumberGenericTableElements;
    ULONG DepthOfTree;
    PRTL_BALANCED_LINKS RestartKey;
    ULONG DeleteCount;
    PRTL_AVL_COMPARE_ROUTINE CompareRoutine;
    PRTL_AVL_ALLOCATE_ROUTINE AllocateRoutine;
    PRTL_AVL_FREE_ROUTINE FreeRoutine;
    PVOID TableContext;
} RTL_AVL_TABLE, *PRTL_AVL_TABLE;

#define try      /* SEH is unavailable freestanding */
#define except(a) if (0)
#define finally  if (1)
#define leave    break



typedef RTL_GENERIC_COMPARE_RESULTS (NTAPI *RTL_COMPARE_ARRAY_ELEMENT_FUNCTION)(PVOID, const void *, const void *, SIZE_T);
typedef int (__cdecl *RTL_COMPARE_ARRAY_ELEMENT)(const void *, const void *, SIZE_T);
typedef void (*RTL_COPY_ARRAY_ELEMENT_FUNCTION)(PVOID, void *, const void *, SIZE_T);


/* --- NLS hooks string.c expects from the kernel ------------------------ */
/* With NlsMbCodePageTag FALSE the ACP is single-byte, and RtlUpperChar /
   RtlUpcaseUnicodeChar take their ASCII fast path, so the tables below are
   never dereferenced. The identity table keeps the symbols resolvable. */
static unsigned short NlsAnsiToUnicodeDataTable[256];
static unsigned short *NlsAnsiToUnicodeData = NlsAnsiToUnicodeDataTable;
static BOOLEAN NlsMbCodePageTag = 0;
static char NlsUnicodeToAnsiDataTable[256];
static char *NlsUnicodeToAnsiData = NlsUnicodeToAnsiDataTable;
static unsigned short NlsLeadByteInfoTable[256];
static unsigned short *const NlsLeadByteInfo = NlsLeadByteInfoTable;

static __inline__ unsigned long wcslen_(const unsigned short *s) {
    unsigned long n = 0; while (s[n]) n++; return n;
}
#define wcslen wcslen_


static unsigned short NlsUnicodeToMbAnsiDataTable[256];
static unsigned short *NlsUnicodeToMbAnsiData = NlsUnicodeToMbAnsiDataTable;
/* ASCII upcase: the path used when NlsMbCodePageTag is FALSE */
#define NLS_UPCASE(c) (((c) >= 'a' && (c) <= 'z') ? (unsigned short)((c) - 0x20) : (unsigned short)(c))
#ifndef HIBYTE
#define HIBYTE(w) ((unsigned char)(((unsigned short)(w) >> 8) & 0xff))
#endif
#ifndef LOBYTE
#define LOBYTE(w) ((unsigned char)((w) & 0xff))
#endif
#ifndef MAKELONG
#define MAKELONG(a, b) ((unsigned long)(((unsigned short)(a)) | ((unsigned long)((unsigned short)(b))) << 16))
#endif


/* ANSI string helpers used by RtlCreateUnicodeStringFromAsciiz */
static __inline__ unsigned long strlen_(const char *s) { unsigned long n = 0; while (s[n]) n++; return n; }
#define strlen strlen_
static __inline__ void RtlInitAnsiString(STRING *d, const char *s) {
    unsigned long n = strlen_(s);
    d->Length = (unsigned short)n; d->MaximumLength = (unsigned short)(n + 1); d->Buffer = (char *)s;
}
static __inline__ NTSTATUS RtlAnsiStringToUnicodeString(UNICODE_STRING *d, const STRING *s, BOOLEAN alloc) {
    unsigned long i;
    (void)alloc;
    if (!d->Buffer) return STATUS_INVALID_PARAMETER;
    if ((unsigned long)s->Length * 2 > d->MaximumLength) return STATUS_BUFFER_OVERFLOW;
    for (i = 0; i < s->Length; i++) d->Buffer[i] = (WCHAR)(unsigned char)s->Buffer[i];
    d->Length = (unsigned short)(s->Length * 2);
    return STATUS_SUCCESS;
}


/* memory primitives the RTL calls; the real ones live in the kernel's own
   RtlMoveMemory. Inline equivalents keep this a pure type+helper header. */
static __inline__ void RtlMoveMemory_(void *d, const void *s, unsigned long n) {
    unsigned char *dp = (unsigned char *)d; const unsigned char *sp = (const unsigned char *)s;
    while (n--) *dp++ = *sp++;
}
static __inline__ void RtlZeroMemory_(void *d, unsigned long n) {
    unsigned char *dp = (unsigned char *)d; while (n--) *dp++ = 0;
}
static __inline__ void RtlFillMemory_(void *d, unsigned long n, unsigned char v) {
    unsigned char *dp = (unsigned char *)d; while (n--) *dp++ = v;
}
#define RtlZeroMemory RtlZeroMemory_
#define RtlFillMemory RtlFillMemory_
#define RtlCopyMemory RtlMoveMemory_
#define RtlMoveMemory RtlMoveMemory_

/* additional RTL data-structure vocabulary (layouts from the tree's ntrtl.h) */
typedef struct _UNICODE_PREFIX_TABLE_ENTRY {
    CSHORT NodeTypeCode;
    CSHORT NameLength;
    struct _UNICODE_PREFIX_TABLE_ENTRY *NextPrefixTree;
    struct _UNICODE_PREFIX_TABLE_ENTRY *CaseMatch;
    RTL_SPLAY_LINKS Links;
    PUNICODE_STRING Prefix;
} UNICODE_PREFIX_TABLE_ENTRY, *PUNICODE_PREFIX_TABLE_ENTRY;

typedef struct _UNICODE_PREFIX_TABLE {
    CSHORT NodeTypeCode;
    CSHORT NameLength;
    PUNICODE_PREFIX_TABLE_ENTRY NextPrefixTree;
    PUNICODE_PREFIX_TABLE_ENTRY LastNextEntry;
} UNICODE_PREFIX_TABLE, *PUNICODE_PREFIX_TABLE;

typedef enum _TABLE_SEARCH_RESULT {
    TableEmptyTree, TableFoundNode, TableInsertAsLeft, TableInsertAsRight
} TABLE_SEARCH_RESULT;

typedef enum _RTL_GENERIC_COMPARE_RESULTS2 { GenericLessThan2, GenericGreaterThan2, GenericEqual2 }
 RTL_GENERIC_COMPARE_RESULTS2;


/* forward declarations bitmap.c uses before their definitions */
VOID RtlClearBits(PRTL_BITMAP BitMapHeader, ULONG StartingIndex, ULONG NumberToClear);
VOID RtlSetBits(PRTL_BITMAP BitMapHeader, ULONG StartingIndex, ULONG NumberToSet);
VOID RtlSetAllBits(PRTL_BITMAP BitMapHeader);
VOID RtlClearAllBits(PRTL_BITMAP BitMapHeader);
ULONG RtlFindClearBits(PRTL_BITMAP BitMapHeader, ULONG NumberToFind, ULONG HintIndex);
ULONG RtlFindSetBits(PRTL_BITMAP BitMapHeader, ULONG NumberToFind, ULONG HintIndex);
ULONG RtlFindFirstRunClear(PRTL_BITMAP BitMapHeader, PULONG StartingIndex);


typedef struct _RTL_BITMAP_RUN { ULONG StartingIndex; ULONG NumberOfBits; } RTL_BITMAP_RUN, *PRTL_BITMAP_RUN;


ULONG RtlFindNextForwardRunClear(PRTL_BITMAP, ULONG, PULONG);
ULONG RtlFindNextForwardRunSet(PRTL_BITMAP, ULONG, PULONG);
ULONG RtlFindLastBackwardRunClear(PRTL_BITMAP, ULONG, PULONG);
ULONG RtlNumberOfClearBits(PRTL_BITMAP);
ULONG RtlNumberOfSetBits(PRTL_BITMAP);
BOOLEAN RtlTestBit(PRTL_BITMAP, ULONG);




PVOID RtlInsertElementGenericTable(PRTL_AVL_TABLE, PVOID, ULONG, PBOOLEAN);
PVOID RtlLookupElementGenericTable(PRTL_AVL_TABLE, PVOID);
BOOLEAN RtlDeleteElementGenericTable(PRTL_AVL_TABLE, PVOID);
PVOID RtlEnumerateGenericTable(PRTL_AVL_TABLE, BOOLEAN);
ULONG RtlNumberGenericTableElements(PRTL_AVL_TABLE);


PVOID RtlInsertElementGenericTableFullAvl(PRTL_AVL_TABLE, PVOID, ULONG, PBOOLEAN, PVOID, TABLE_SEARCH_RESULT);
PVOID RtlLookupElementGenericTableFullAvl(PRTL_AVL_TABLE, PVOID, PVOID *, TABLE_SEARCH_RESULT *);
PVOID RtlInsertElementGenericTableAvl(PRTL_AVL_TABLE, PVOID, ULONG, PBOOLEAN);
PVOID RtlLookupElementGenericTableAvl(PRTL_AVL_TABLE, PVOID);
BOOLEAN RtlDeleteElementGenericTableAvl(PRTL_AVL_TABLE, PVOID);
PVOID RtlEnumerateGenericTableAvl(PRTL_AVL_TABLE, BOOLEAN);
ULONG RtlNumberGenericTableElementsAvl(PRTL_AVL_TABLE);


PVOID RtlEnumerateGenericTableWithoutSplayingAvl(PRTL_AVL_TABLE, PVOID *);
PVOID RtlEnumerateGenericTableWithoutSplaying(PRTL_AVL_TABLE, PVOID *);
PVOID RtlGetElementGenericTable(PRTL_AVL_TABLE, ULONG);
PVOID RtlGetElementGenericTableAvl(PRTL_AVL_TABLE, ULONG);


PVOID RtlLookupFirstMatchingElementGenericTableAvl(PRTL_AVL_TABLE, PVOID, PVOID *);


/* bitmap.c helpers: the bit-count tables and a no-op ASSERT */
#define ASSERT(e) ((void)0)
static __inline__ void RtlFillMemoryUlong(unsigned long *d, unsigned long n, unsigned long v) {
    unsigned long i, c = n / 4; for (i = 0; i < c; i++) d[i] = v;
}


/* tables and macros from base/ntos/rtl/ntrtlp.h that bitmap.c depends on */
extern const CCHAR RtlpBitsClearAnywhere[256];
extern const CCHAR RtlpBitsClearLow[256];
extern const CCHAR RtlpBitsClearHigh[256];
extern const CCHAR RtlpBitsClearTotal[256];
#define RtlpBitSetAnywhere(Byte) RtlpBitsClearAnywhere[(~(Byte)) & 0xFF]
#define RtlpBitsSetLow(Byte)     RtlpBitsClearLow[(~(Byte)) & 0xFF]
#define RtlpBitsSetHigh(Byte)    RtlpBitsClearHigh[(~(Byte)) & 0xFF]
#define RtlpBitsSetTotal(Byte)   RtlpBitsClearTotal[(~(Byte)) & 0xFF]


/* RtlCheckBit is a kernel export; bitmap.c calls it without defining it. */
static __inline__ unsigned char RtlCheckBit_prv(PRTL_BITMAP b, ULONG i) {
    return (unsigned char)((b->Buffer[i / 32] >> (i % 32)) & 1);
}
#define RtlCheckBit RtlCheckBit_prv


/* splay/AVL link predicates from ntrtlp.h */
#define RtlIsLeftChild(L)  ((L)->Parent != 0 && (L)->Parent->LeftChild == (L))
#define RtlIsRightChild(L) ((L)->Parent != 0 && (L)->Parent->RightChild == (L))


/* splay link accessors, verbatim macro bodies from public/sdk/inc/ntrtl.h */
#define RtlParent(Links)    ((struct _RTL_SPLAY_LINKS *)(Links)->Parent)
#define RtlLeftChild(Links) ((struct _RTL_SPLAY_LINKS *)(Links)->LeftChild)
#define RtlRightChild(Links)((struct _RTL_SPLAY_LINKS *)(Links)->RightChild)
#define RtlIsRoot(Links)    ((Links)->Parent == 0)


/* splay-tree primitives: macro bodies verbatim from public/sdk/inc/ntrtl.h */
#define RtlInitializeSplayLinks(Links) {                     \
    struct _RTL_SPLAY_LINKS *_S = (struct _RTL_SPLAY_LINKS *)(Links); \
    _S->Parent = _S; _S->LeftChild = 0; _S->RightChild = 0;  \
}
#define RtlInsertAsLeftChild(ParentLinks, ChildLinks) {      \
    struct _RTL_SPLAY_LINKS *_P = (struct _RTL_SPLAY_LINKS *)(ParentLinks); \
    struct _RTL_SPLAY_LINKS *_C = (struct _RTL_SPLAY_LINKS *)(ChildLinks);  \
    _P->LeftChild = _C; _C->Parent = _P;                     \
}
#define RtlInsertAsRightChild(ParentLinks, ChildLinks) {     \
    struct _RTL_SPLAY_LINKS *_P = (struct _RTL_SPLAY_LINKS *)(ParentLinks); \
    struct _RTL_SPLAY_LINKS *_C = (struct _RTL_SPLAY_LINKS *)(ChildLinks);  \
    _P->RightChild = _C; _C->Parent = _P;                    \
}


static __inline__ unsigned long RtlCompareMemory(const void *a, const void *b, unsigned long n) {
    const unsigned char *x = (const unsigned char *)a, *y = (const unsigned char *)b;
    unsigned long i = 0; while (i < n && x[i] == y[i]) i++; return i;
}


struct _RTL_SPLAY_LINKS *RtlSubtreePredecessor(struct _RTL_SPLAY_LINKS *);
struct _RTL_SPLAY_LINKS *RtlSubtreeSuccessor(struct _RTL_SPLAY_LINKS *);
struct _RTL_SPLAY_LINKS *RtlRealPredecessor(struct _RTL_SPLAY_LINKS *);
struct _RTL_SPLAY_LINKS *RtlRealSuccessor(struct _RTL_SPLAY_LINKS *);


/* --- in-guest tracing ---------------------------------------------------
 * DbgPrint/DbgPrintEx write to the QEMU debug console (port 0xE9). XP sources
 * and the test harness can then be observed without a debugger, which is what
 * the prefix.c investigation lacked.
 */
static __inline__ void xp_dbg_putc(char c) { __asm__ volatile("outb %0,%1" :: "a"((unsigned char)c), "Nd"((unsigned short)0xE9)); }
static __inline__ void xp_dbg_hex(unsigned long v) {
    const char *d = "0123456789ABCDEF"; int i, st = 0;
    for (i = 28; i >= 0; i -= 4) { unsigned long n = (v >> i) & 0xF;
        if (n || st || i == 0) { st = 1; xp_dbg_putc(d[n]); } }
}
static __inline__ unsigned long DbgPrint(const char *fmt, ...) {
    va_list ap; const char *p = fmt;
    va_start(ap, fmt);
    while (*p) {
        if (*p == '%' && p[1] == 'x') { xp_dbg_hex((unsigned long)va_arg(ap, unsigned)); p += 2; }
        else if (*p == '%' && p[1] == 's') { const char *s2 = va_arg(ap, const char *); while (*s2) xp_dbg_putc(*s2++); p += 2; }
        else { xp_dbg_putc(*p++); }
    }
    va_end(ap);
    return 0;
}
#define DbgPrintEx(a, b, ...) DbgPrint(__VA_ARGS__)

#endif /* _XPKERNEL_H_ */
