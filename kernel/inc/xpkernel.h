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

/* SEH emulation:  try { ... } except(a) { handler }
   expands to  if (1) do { { ... } } while(0); else { handler }
   The handler never runs (no real exceptions in the freestanding
   environment), and `leave` (break) exits the do-while, landing past the
   handler / at the finally block — exactly like SEH __leave.
   try/finally-only becomes  if (1) do { { ... } } while(0); { finally body }. */
#define try      if (1) do {
#define except(a) } while(0); else
#define finally  } while(0);
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
/* VERBATIM from the tree's ntrtl.h: the root's Parent points at ITSELF,
   not NULL. Getting this wrong makes RtlSplay loop forever. */
#define RtlIsRoot(Links) ((struct _RTL_SPLAY_LINKS *)(Links) == \
                          (struct _RTL_SPLAY_LINKS *)(Links)->Parent)


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


/* --- TEB, as far as the RTL needs it ----------------------------------
 * error.c reads and writes Teb->LastStatusValue. A real TEB lives in user
 * space and is reached via fs:[0x18]; here a static stands in so the RTL logic
 * (error translation) can run and be verified. */
typedef struct _XP_TEB_STUB {
    unsigned long Dummy[2];
    NTSTATUS LastStatusValue;
} XP_TEB_STUB, *PTEB;

static XP_TEB_STUB XpTeb;
static __inline__ PTEB NtCurrentTeb_(void) { return &XpTeb; }
#define NtCurrentTeb NtCurrentTeb_

/* STATUS codes needed by the RTL; values taken verbatim from the tree's
   public/sdk/inc/ntstatus.h so they are exact, not invented. */
#define STATUS_ARRAY_BOUNDS_EXCEEDED   ((NTSTATUS)0xC000008CL)
#define STATUS_BREAKPOINT             ((NTSTATUS)0x80000003L)
#define STATUS_FLOAT_DENORMAL_OPERAND ((NTSTATUS)0xC000008DL)
#define STATUS_FLOAT_DIVIDE_BY_ZERO   ((NTSTATUS)0xC000008EL)
#define STATUS_FLOAT_INEXACT_RESULT   ((NTSTATUS)0xC000008FL)
#define STATUS_ILLEGAL_INSTRUCTION    ((NTSTATUS)0xC000001DL)
#define STATUS_INVALID_DISPOSITION    ((NTSTATUS)0xC0000026L)
#define STATUS_NONCONTINUABLE_EXCEPTION ((NTSTATUS)0xC0000025L)
#define STATUS_PARITY_ERROR           ((NTSTATUS)0xC000002BL)
#define STATUS_SINGLE_STEP            ((NTSTATUS)0x80000004L)

#define STATUS_ACCESS_VIOLATION ((NTSTATUS)((NTSTATUS)0xC0000005L))
#define STATUS_DATATYPE_MISALIGNMENT ((NTSTATUS)((NTSTATUS)0x80000002L))
#define STATUS_DATATYPE_MISALIGNMENT_ERROR ((NTSTATUS)((NTSTATUS)0xC00002C5L))
#define STATUS_FLOAT_INVALID_OPERATION ((NTSTATUS)((NTSTATUS)0xC0000090L))
#define STATUS_FLOAT_OVERFLOW ((NTSTATUS)((NTSTATUS)0xC0000091L))
#define STATUS_FLOAT_STACK_CHECK ((NTSTATUS)((NTSTATUS)0xC0000092L))
#define STATUS_FLOAT_UNDERFLOW ((NTSTATUS)((NTSTATUS)0xC0000093L))
#define STATUS_GUARD_PAGE_VIOLATION ((NTSTATUS)((NTSTATUS)0x80000001L))
#define STATUS_INTEGER_DIVIDE_BY_ZERO ((NTSTATUS)((NTSTATUS)0xC0000094L))
#define STATUS_PRIVILEGED_INSTRUCTION ((NTSTATUS)((NTSTATUS)0xC0000096L))

#define STATUS_BAD_WORKING_SET_LIMIT ((NTSTATUS)((NTSTATUS)0xC000004CL))
#define STATUS_CTL_FILE_NOT_SUPPORTED ((NTSTATUS)((NTSTATUS)0xC0000057L))
#define STATUS_INCOMPATIBLE_FILE_MAP ((NTSTATUS)((NTSTATUS)0xC000004DL))
#define STATUS_NOT_LOCKED ((NTSTATUS)((NTSTATUS)0xC000002AL))
#define STATUS_PORT_ALREADY_SET ((NTSTATUS)((NTSTATUS)0xC0000048L))
#define STATUS_PORT_DISCONNECTED ((NTSTATUS)((NTSTATUS)0xC0000037L))
#define STATUS_SECTION_NOT_IMAGE ((NTSTATUS)((NTSTATUS)0xC0000049L))
#define STATUS_WORKING_SET_LIMIT_RANGE ((NTSTATUS)((NTSTATUS)0x40000002L))

#define STATUS_MORE_PROCESSING_REQUIRED ((NTSTATUS)((NTSTATUS)0xC0000016L))
#define STATUS_NOT_MAPPED_VIEW ((NTSTATUS)((NTSTATUS)0xC0000019L))
#define STATUS_UNABLE_TO_DELETE_SECTION ((NTSTATUS)((NTSTATUS)0xC000001BL))
#define STATUS_UNABLE_TO_FREE_VM ((NTSTATUS)((NTSTATUS)0xC000001AL))

#define STATUS_INVALID_CID ((NTSTATUS)((NTSTATUS)0xC000000BL))
#define STATUS_STACK_OVERFLOW ((NTSTATUS)((NTSTATUS)0xC00000FDL))

#define STATUS_BAD_INITIAL_STACK ((NTSTATUS)((NTSTATUS)0xC0000009L))

#define STATUS_INVALID_VOLUME_LABEL ((NTSTATUS)((NTSTATUS)0xC0000086L))

#define ERROR_INVALID_ADDRESS ((ULONG)487L)
#define ERROR_INVALID_HANDLE ((ULONG)6L)
#define ERROR_INVALID_PARAMETER ((ULONG)87L)
#define ERROR_LABEL_TOO_LONG ((ULONG)154L)
#define ERROR_MORE_DATA ((ULONG)234L)
#define ERROR_NOACCESS ((ULONG)998L)
#define ERROR_NOT_LOCKED ((ULONG)158L)
#define ERROR_NOT_SUPPORTED ((ULONG)50L)
#define ERROR_STACK_OVERFLOW ((ULONG)1001L)

#define ERROR_BAD_LENGTH ((ULONG)24L)
#define ERROR_INVALID_THREAD_ID ((ULONG)1444L)
#define ERROR_KEY_DELETED ((ULONG)1018L)
#define ERROR_NOTIFY_ENUM_DIR ((ULONG)1022L)
#define ERROR_NOT_REGISTRY_FILE ((ULONG)1017L)
#define ERROR_OUTOFMEMORY ((ULONG)14L)
#define ERROR_REGISTRY_IO_FAILED ((ULONG)1016L)
#define ERROR_REGISTRY_RECOVERED ((ULONG)1014L)
#define ERROR_SIGNAL_REFUSED ((ULONG)156L)

#define ERROR_BADDB ((ULONG)1009L)
#define ERROR_CHILD_MUST_BE_VOLATILE ((ULONG)1021L)
#define ERROR_KEY_HAS_CHILDREN ((ULONG)1020L)
#define ERROR_MOD_NOT_FOUND ((ULONG)126L)
#define ERROR_NO_LOG_SPACE ((ULONG)1019L)

#define ERROR_DLL_INIT_FAILED ((ULONG)1114L)
#define ERROR_INVALID_IMPORT_OF_NON_DLL ((ULONG)1276L)

#define ERROR_INVALID_ORDINAL ((ULONG)182L)

#define STATUS_CHILD_MUST_BE_VOLATILE ((NTSTATUS)0xC0000181)
#define STATUS_DLL_INIT_FAILED ((NTSTATUS)0xC0000142)
#define STATUS_DLL_NOT_FOUND ((NTSTATUS)0xC0000135)
#define STATUS_DRIVER_ORDINAL_NOT_FOUND ((NTSTATUS)0xC0000262)
#define STATUS_DRIVER_UNABLE_TO_LOAD ((NTSTATUS)0xC000026C)
#define STATUS_INVALID_IMPORT_OF_NON_DLL ((NTSTATUS)0xC000036F)
#define STATUS_KEY_DELETED ((NTSTATUS)0xC000017C)
#define STATUS_KEY_HAS_CHILDREN ((NTSTATUS)0xC0000180)
#define STATUS_NOTIFY_ENUM_DIR ((NTSTATUS)0x0000010C)
#define STATUS_NOT_MAPPED_DATA ((NTSTATUS)0xC0000088)
#define STATUS_NOT_REGISTRY_FILE ((NTSTATUS)0xC000015C)
#define STATUS_NO_LDT ((NTSTATUS)0xC0000117)
#define STATUS_NO_LOG_SPACE ((NTSTATUS)0xC000017D)
#define STATUS_ORDINAL_NOT_FOUND ((NTSTATUS)0xC0000138)
#define STATUS_REGISTRY_CORRUPT ((NTSTATUS)0xC000014C)
#define STATUS_REGISTRY_IO_FAILED ((NTSTATUS)0xC000014D)
#define STATUS_REGISTRY_RECOVERED ((NTSTATUS)0x40000009)
#define STATUS_SECTION_NOT_EXTENDED ((NTSTATUS)0xC0000087)
#define STATUS_SUSPEND_COUNT_EXCEEDED ((NTSTATUS)0xC000004A)

#define ERROR_ARITHMETIC_OVERFLOW ((ULONG)534L)
#define ERROR_BAD_DRIVER ((ULONG)2001L)
#define ERROR_IO_PENDING ((ULONG)997L)
#define ERROR_NO_INHERITANCE ((ULONG)1391L)
#define ERROR_NO_MORE_FILES ((ULONG)18L)
#define ERROR_NO_MORE_ITEMS ((ULONG)259L)
#define ERROR_PROC_NOT_FOUND ((ULONG)127L)
#define STATUS_DRIVER_ENTRYPOINT_NOT_FOUND ((NTSTATUS)0xC0000263)
#define STATUS_ENTRYPOINT_NOT_FOUND ((NTSTATUS)0xC0000139)
#define STATUS_GUIDS_EXHAUSTED ((NTSTATUS)0xC0000083)
#define STATUS_INTEGER_OVERFLOW ((NTSTATUS)0xC0000095)
#define STATUS_MORE_ENTRIES ((NTSTATUS)0x00000105)
#define STATUS_NO_INHERITANCE ((NTSTATUS)0x8000000B)
#define STATUS_NO_MORE_EAS ((NTSTATUS)0x80000012)
#define STATUS_NO_MORE_ENTRIES ((NTSTATUS)0x8000001A)
#define STATUS_NO_MORE_FILES ((NTSTATUS)0x80000006)
#define STATUS_PENDING ((NTSTATUS)0x00000103)

#define ERROR_COMMITMENT_LIMIT ((ULONG)1455L)
#define ERROR_GEN_FAILURE ((ULONG)31L)
#define ERROR_INVALID_FUNCTION ((ULONG)1L)
#define ERROR_NOT_ENOUGH_MEMORY ((ULONG)8L)
#define ERROR_PAGEFILE_QUOTA ((ULONG)1454L)
#define ERROR_SWAPERROR ((ULONG)999L)
#define ERROR_TOO_MANY_LINKS ((ULONG)1142L)
#define STATUS_AGENTS_EXHAUSTED ((NTSTATUS)0xC0000085)
#define STATUS_COMMITMENT_LIMIT ((NTSTATUS)0xC000012D)
#define STATUS_FILE_CLOSED ((NTSTATUS)0xC0000128)
#define STATUS_ILLEGAL_FUNCTION ((NTSTATUS)0xC00000AF)
#define STATUS_IN_PAGE_ERROR ((NTSTATUS)0xC0000006)
#define STATUS_OBJECT_TYPE_MISMATCH ((NTSTATUS)0xC0000024)
#define STATUS_PAGEFILE_QUOTA ((NTSTATUS)0xC0000007)
#define STATUS_SECTION_TOO_BIG ((NTSTATUS)0xC0000040)
#define STATUS_TOO_MANY_LINKS ((NTSTATUS)0xC0000265)

#define ERROR_INTERNAL_ERROR ((ULONG)1359L)
#define ERROR_PARTIAL_COPY ((ULONG)299L)
#define STATUS_HANDLE_NOT_CLOSABLE ((NTSTATUS)0xC0000235)
#define STATUS_INVALID_PARAMETER_1 ((NTSTATUS)0xC00000EF)
#define STATUS_INVALID_PARAMETER_10 ((NTSTATUS)0xC00000F8)
#define STATUS_INVALID_PARAMETER_2 ((NTSTATUS)0xC00000F0)
#define STATUS_INVALID_PARAMETER_3 ((NTSTATUS)0xC00000F1)
#define STATUS_INVALID_PARAMETER_4 ((NTSTATUS)0xC00000F2)
#define STATUS_INVALID_PARAMETER_5 ((NTSTATUS)0xC00000F3)
#define STATUS_INVALID_PARAMETER_6 ((NTSTATUS)0xC00000F4)
#define STATUS_INVALID_PARAMETER_7 ((NTSTATUS)0xC00000F5)
#define STATUS_INVALID_PARAMETER_8 ((NTSTATUS)0xC00000F6)
#define STATUS_INVALID_PARAMETER_9 ((NTSTATUS)0xC00000F7)
#define STATUS_INVALID_PORT_HANDLE ((NTSTATUS)0xC0000042)
#define STATUS_LPC_REPLY_LOST ((NTSTATUS)0xC0000253)
#define STATUS_NOT_COMMITTED ((NTSTATUS)0xC000002D)
#define STATUS_PARTIAL_COPY ((NTSTATUS)0x8000000D)

#define ERROR_FILE_NOT_FOUND ((ULONG)2L)
#define ERROR_RESOURCE_DATA_NOT_FOUND ((ULONG)1812L)
#define ERROR_RESOURCE_LANG_NOT_FOUND ((ULONG)1815L)
#define ERROR_RESOURCE_NAME_NOT_FOUND ((ULONG)1814L)
#define ERROR_RESOURCE_TYPE_NOT_FOUND ((ULONG)1813L)
#define STATUS_INVALID_PAGE_PROTECTION ((NTSTATUS)0xC0000045)
#define STATUS_INVALID_PARAMETER_11 ((NTSTATUS)0xC00000F9)
#define STATUS_INVALID_PARAMETER_12 ((NTSTATUS)0xC00000FA)
#define STATUS_INVALID_PARAMETER_MIX ((NTSTATUS)0xC0000030)
#define STATUS_NO_SUCH_DEVICE ((NTSTATUS)0xC000000E)
#define STATUS_NO_SUCH_FILE ((NTSTATUS)0xC000000F)
#define STATUS_RESOURCE_DATA_NOT_FOUND ((NTSTATUS)0xC0000089)
#define STATUS_RESOURCE_LANG_NOT_FOUND ((NTSTATUS)0xC0000204)
#define STATUS_RESOURCE_NAME_NOT_FOUND ((NTSTATUS)0xC000008B)
#define STATUS_RESOURCE_TYPE_NOT_FOUND ((NTSTATUS)0xC000008A)
#define STATUS_SECTION_PROTECTION ((NTSTATUS)0xC000004E)

#define ERROR_HANDLE_EOF ((ULONG)38L)
#define ERROR_NOT_READY ((ULONG)21L)
#define ERROR_NO_MEDIA_IN_DRIVE ((ULONG)1112L)
#define ERROR_SECTOR_NOT_FOUND ((ULONG)27L)
#define ERROR_WRONG_DISK ((ULONG)34L)
#define STATUS_END_OF_FILE ((NTSTATUS)0xC0000011)
#define STATUS_FILE_FORCED_CLOSED ((NTSTATUS)0xC00000B6)
#define STATUS_INVALID_DEVICE_REQUEST ((NTSTATUS)0xC0000010)
#define STATUS_NONEXISTENT_SECTOR ((NTSTATUS)0xC0000015)
#define STATUS_NO_MEDIA ((NTSTATUS)0xC0000178)
#define STATUS_NO_MEDIA_IN_DEVICE ((NTSTATUS)0xC0000013)
#define STATUS_VOLUME_DISMOUNTED ((NTSTATUS)0xC000026E)
#define STATUS_VOLUME_NOT_UPGRADED ((NTSTATUS)0xC000029C)
#define STATUS_WORKING_SET_QUOTA ((NTSTATUS)0xC00000A1)
#define STATUS_WRONG_VOLUME ((NTSTATUS)0xC0000012)

#define ERROR_ACCESS_DENIED ((ULONG)5L)
#define ERROR_WORKING_SET_QUOTA ((ULONG)1453L)
#define STATUS_ALREADY_COMMITTED ((NTSTATUS)0xC0000021)
#define STATUS_CANNOT_DELETE ((NTSTATUS)0xC0000121)
#define STATUS_CONFLICTING_ADDRESSES ((NTSTATUS)0xC0000018)
#define STATUS_FILE_IS_A_DIRECTORY ((NTSTATUS)0xC00000BA)
#define STATUS_INVALID_LOCK_SEQUENCE ((NTSTATUS)0xC000001E)
#define STATUS_INVALID_SYSTEM_SERVICE ((NTSTATUS)0xC000001C)
#define STATUS_INVALID_VIEW_SIZE ((NTSTATUS)0xC000001F)
#define STATUS_PROCESS_IS_TERMINATING ((NTSTATUS)0xC000010A)
#define STATUS_THREAD_IS_TERMINATING ((NTSTATUS)0xC000004B)

#define ERROR_CANNOT_IMPERSONATE ((ULONG)1368L)
#define ERROR_INVALID_COMPUTERNAME ((ULONG)1210L)
#define ERROR_LOGON_FAILURE ((ULONG)1326L)
#define ERROR_MUTUAL_AUTH_FAILED ((ULONG)1397L)
#define ERROR_NO_SUCH_PRIVILEGE ((ULONG)1313L)
#define ERROR_PRIVILEGE_NOT_HELD ((ULONG)1314L)
#define ERROR_TIME_SKEW ((ULONG)1398L)
#define STATUS_CANNOT_IMPERSONATE ((NTSTATUS)0xC000010D)
#define STATUS_DELETE_PENDING ((NTSTATUS)0xC0000056)
#define STATUS_FILE_DELETED ((NTSTATUS)0xC0000123)
#define STATUS_FILE_RENAMED ((NTSTATUS)0xC00000D5)
#define STATUS_INVALID_COMPUTER_NAME ((NTSTATUS)0xC0000122)
#define STATUS_LOGON_FAILURE ((NTSTATUS)0xC000006D)
#define STATUS_MUTUAL_AUTHENTICATION_FAILED ((NTSTATUS)0xC00002C3)
#define STATUS_NO_SUCH_PRIVILEGE ((NTSTATUS)0xC0000060)
#define STATUS_PORT_CONNECTION_REFUSED ((NTSTATUS)0xC0000041)
#define STATUS_TIME_DIFFERENCE_AT_DC ((NTSTATUS)0xC0000133)

#define ERROR_DOWNGRADE_DETECTED ((ULONG)1265L)
#define ERROR_PKINIT_FAILURE ((ULONG)1263L)
#define ERROR_SMARTCARD_SUBSYSTEM_FAILURE ((ULONG)1264L)
#define STATUS_DOWNGRADE_DETECTED ((NTSTATUS)0xC0000388)
#define STATUS_ISSUING_CA_UNTRUSTED ((NTSTATUS)0xC000038A)
#define STATUS_NO_KERB_KEY ((NTSTATUS)0xC0000322)
#define STATUS_PKINIT_CLIENT_FAILURE ((NTSTATUS)0xC000038C)
#define STATUS_PKINIT_FAILURE ((NTSTATUS)0xC0000320)
#define STATUS_REVOCATION_OFFLINE_C ((NTSTATUS)0xC000038B)
#define STATUS_SMARTCARD_CERT_EXPIRED ((NTSTATUS)0xC000038D)
#define STATUS_SMARTCARD_CERT_REVOKED ((NTSTATUS)0xC0000389)
#define STATUS_SMARTCARD_SUBSYSTEM_FAILURE ((NTSTATUS)0xC0000321)

#define ERROR_CURRENT_DOMAIN_NOT_ALLOWED ((ULONG)1399L)
#define STATUS_CURRENT_DOMAIN_NOT_ALLOWED ((NTSTATUS)0xC00002E9)
#define STATUS_SMARTCARD_CARD_BLOCKED ((NTSTATUS)0xC0000381)
#define STATUS_SMARTCARD_CARD_NOT_AUTHENTICATED ((NTSTATUS)0xC0000382)
#define STATUS_SMARTCARD_NO_CARD ((NTSTATUS)0xC0000383)
#define STATUS_SMARTCARD_NO_KEY_CONTAINER ((NTSTATUS)0xC0000384)
#define STATUS_SMARTCARD_WRONG_PIN ((NTSTATUS)0xC0000380)

#define STATUS_SMARTCARD_IO_ERROR ((NTSTATUS)0xC0000387)
#define STATUS_SMARTCARD_NO_CERTIFICATE ((NTSTATUS)0xC0000385)
#define STATUS_SMARTCARD_NO_KEYSET ((NTSTATUS)0xC0000386)

#define ERROR_ACCOUNT_RESTRICTION ((ULONG)1327L)
#define STATUS_ACCOUNT_RESTRICTION ((NTSTATUS)0xC000006E)
#define STATUS_INVALID_LOGON_HOURS ((NTSTATUS)0xC000006F)

#define ERROR_INVALID_LOGON_HOURS ((ULONG)1328L)
#define ERROR_INVALID_WORKSTATION ((ULONG)1329L)
#define STATUS_INVALID_WORKSTATION ((NTSTATUS)0xC0000070)

#define ERROR_INSUFFICIENT_BUFFER ((ULONG)122L)
#define STATUS_DISK_CORRUPT_ERROR ((NTSTATUS)0xC0000032)
#define STATUS_UNABLE_TO_DECOMMIT_VM ((NTSTATUS)0xC000002C)

#define ERROR_DISK_CORRUPT ((ULONG)1393L)
#define ERROR_IO_DEVICE ((ULONG)1117L)
#define STATUS_FT_MISSING_MEMBER ((NTSTATUS)0xC000015F)

#define ERROR_ENVVAR_NOT_FOUND ((ULONG)203L)
#define STATUS_FT_ORPHANING ((NTSTATUS)0xC000016D)
#define STATUS_VARIABLE_NOT_FOUND ((NTSTATUS)0xC0000100)

#define ERROR_ALREADY_EXISTS ((ULONG)183L)
#define ERROR_INVALID_NAME ((ULONG)123L)
#define STATUS_OBJECT_PATH_INVALID ((NTSTATUS)0xC0000039)

#define ERROR_BAD_PATHNAME ((ULONG)161L)
#define ERROR_PATH_NOT_FOUND ((ULONG)3L)
#define STATUS_DFS_EXIT_PATH_FOUND ((NTSTATUS)0xC000009B)

#define ERROR_CONNECTION_UNAVAIL ((ULONG)1201L)
#define ERROR_FILENAME_EXCED_RANGE ((ULONG)206L)
#define STATUS_DFS_UNAVAILABLE ((NTSTATUS)0xC000026D)

#define STATUS_DATA_ERROR ((NTSTATUS)0xC000003E)
#define STATUS_DATA_LATE_ERROR ((NTSTATUS)0xC000003D)
#define STATUS_DATA_OVERRUN ((NTSTATUS)0xC000003C)

#define ERROR_CRC ((ULONG)23L)
#define STATUS_CRC_ERROR ((NTSTATUS)0xC000003F)

#define ERROR_SHARING_VIOLATION ((ULONG)32L)
#define STATUS_QUOTA_EXCEEDED ((NTSTATUS)0xC0000044)
#define STATUS_SHARING_VIOLATION ((NTSTATUS)0xC0000043)

#define ERROR_NOT_ENOUGH_QUOTA ((ULONG)1816L)
#define ERROR_NOT_OWNER ((ULONG)288L)
#define STATUS_MUTANT_NOT_OWNED ((NTSTATUS)0xC0000046)

#define ERROR_TOO_MANY_POSTS ((ULONG)298L)
#define STATUS_DISK_FULL ((NTSTATUS)0xC000007F)
#define STATUS_SEMAPHORE_LIMIT_EXCEEDED ((NTSTATUS)0xC0000047)

#define ERROR_DISK_FULL ((ULONG)112L)
#define ERROR_LOCK_VIOLATION ((ULONG)33L)
#define STATUS_LOCK_NOT_GRANTED ((NTSTATUS)0xC0000055)

#define ERROR_DIRECTORY ((ULONG)267L)
#define STATUS_FILE_LOCK_CONFLICT ((NTSTATUS)0xC0000054)
#define STATUS_NOT_A_DIRECTORY ((NTSTATUS)0xC0000103)

#define ERROR_CANNOT_MAKE ((ULONG)82L)
#define STATUS_CANNOT_MAKE ((NTSTATUS)0xC00002EA)
#define STATUS_UNKNOWN_REVISION ((NTSTATUS)0xC0000058)

#define ERROR_REVISION_MISMATCH ((ULONG)1306L)
#define ERROR_UNKNOWN_REVISION ((ULONG)1305L)
#define STATUS_REVISION_MISMATCH ((NTSTATUS)0xC0000059)

#define ERROR_INVALID_OWNER ((ULONG)1307L)
#define STATUS_INVALID_OWNER ((NTSTATUS)0xC000005A)
#define STATUS_INVALID_PRIMARY_GROUP ((NTSTATUS)0xC000005B)

#define ERROR_INVALID_PRIMARY_GROUP ((ULONG)1308L)
#define ERROR_NO_IMPERSONATION_TOKEN ((ULONG)1309L)
#define STATUS_NO_IMPERSONATION_TOKEN ((NTSTATUS)0xC000005C)

#define ERROR_CANT_DISABLE_MANDATORY ((ULONG)1310L)
#define STATUS_CANT_DISABLE_MANDATORY ((NTSTATUS)0xC000005D)
#define STATUS_NO_LOGON_SERVERS ((NTSTATUS)0xC000005E)

#define ERROR_DOMAIN_CONTROLLER_NOT_FOUND ((ULONG)1908L)
#define ERROR_NO_LOGON_SERVERS ((ULONG)1311L)
#define STATUS_DOMAIN_CONTROLLER_NOT_FOUND ((NTSTATUS)0xC0000233)

#define ERROR_NO_SUCH_LOGON_SESSION ((ULONG)1312L)
#define STATUS_INVALID_ACCOUNT_NAME ((NTSTATUS)0xC0000062)
#define STATUS_NO_SUCH_LOGON_SESSION ((NTSTATUS)0xC000005F)

#define ERROR_INVALID_ACCOUNT_NAME ((ULONG)1315L)
#define ERROR_USER_EXISTS ((ULONG)1316L)
#define STATUS_USER_EXISTS ((NTSTATUS)0xC0000063)

#define ERROR_NO_SUCH_USER ((ULONG)1317L)
#define STATUS_GROUP_EXISTS ((NTSTATUS)0xC0000065)
#define STATUS_NO_SUCH_USER ((NTSTATUS)0xC0000064)

#define ERROR_GROUP_EXISTS ((ULONG)1318L)
#define ERROR_NO_SUCH_GROUP ((ULONG)1319L)
#define STATUS_NO_SUCH_GROUP ((NTSTATUS)0xC0000066)

#define ERROR_SPECIAL_GROUP ((ULONG)1372L)
#define STATUS_MEMBER_IN_GROUP ((NTSTATUS)0xC0000067)
#define STATUS_SPECIAL_GROUP ((NTSTATUS)0xC0000125)

#define ERROR_MEMBER_IN_GROUP ((ULONG)1320L)
#define ERROR_MEMBER_NOT_IN_GROUP ((ULONG)1321L)
#define STATUS_MEMBER_NOT_IN_GROUP ((NTSTATUS)0xC0000068)

#define ERROR_LAST_ADMIN ((ULONG)1322L)
#define STATUS_LAST_ADMIN ((NTSTATUS)0xC0000069)
#define STATUS_WRONG_PASSWORD ((NTSTATUS)0xC000006A)

#define ERROR_INVALID_PASSWORD ((ULONG)86L)
#define STATUS_WRONG_PASSWORD_CORE ((NTSTATUS)0xC0000149)

#define ERROR_ILL_FORMED_PASSWORD ((ULONG)1324L)
#define STATUS_ILL_FORMED_PASSWORD ((NTSTATUS)0xC000006B)
#define STATUS_PASSWORD_RESTRICTION ((NTSTATUS)0xC000006C)

#define ERROR_PASSWORD_EXPIRED ((ULONG)1330L)
#define ERROR_PASSWORD_RESTRICTION ((ULONG)1325L)
#define STATUS_PASSWORD_EXPIRED ((NTSTATUS)0xC0000071)

#define ERROR_PASSWORD_MUST_CHANGE ((ULONG)1907L)
#define STATUS_ACCOUNT_DISABLED ((NTSTATUS)0xC0000072)
#define STATUS_PASSWORD_MUST_CHANGE ((NTSTATUS)0xC0000224)

#define ERROR_ACCOUNT_DISABLED ((ULONG)1331L)
#define ERROR_ACCOUNT_LOCKED_OUT ((ULONG)1909L)
#define STATUS_ACCOUNT_LOCKED_OUT ((NTSTATUS)0xC0000234)

#define ERROR_NONE_MAPPED ((ULONG)1332L)
#define STATUS_NONE_MAPPED ((NTSTATUS)0xC0000073)
#define STATUS_TOO_MANY_LUIDS_REQUESTED ((NTSTATUS)0xC0000074)

#define ERROR_LUIDS_EXHAUSTED ((ULONG)1334L)
#define ERROR_TOO_MANY_LUIDS_REQUESTED ((ULONG)1333L)
#define STATUS_LUIDS_EXHAUSTED ((NTSTATUS)0xC0000075)

#define ERROR_INVALID_SUB_AUTHORITY ((ULONG)1335L)
#define STATUS_INVALID_ACL ((NTSTATUS)0xC0000077)
#define STATUS_INVALID_SUB_AUTHORITY ((NTSTATUS)0xC0000076)

#define ERROR_INVALID_ACL ((ULONG)1336L)
#define ERROR_INVALID_SID ((ULONG)1337L)
#define STATUS_INVALID_SID ((NTSTATUS)0xC0000078)

#define ERROR_INVALID_SECURITY_DESCR ((ULONG)1338L)
#define STATUS_INVALID_SECURITY_DESCR ((NTSTATUS)0xC0000079)
#define STATUS_PROCEDURE_NOT_FOUND ((NTSTATUS)0xC000007A)

#define ERROR_BAD_EXE_FORMAT ((ULONG)193L)
#define STATUS_BAD_INITIAL_PC ((NTSTATUS)0xC000000A)
#define STATUS_INVALID_FILE_FOR_SECTION ((NTSTATUS)0xC0000020)

#define ERROR_ACCESS_DISABLED_BY_POLICY ((ULONG)1260L)
#define ERROR_ACCOUNT_EXPIRED ((ULONG)1793L)
#define ERROR_ACTIVE_CONNECTIONS ((ULONG)2402L)
#define ERROR_ADAP_HDW_ERR ((ULONG)57L)
#define ERROR_ADDRESS_ALREADY_ASSOCIATED ((ULONG)1227L)
#define ERROR_ADDRESS_NOT_ASSOCIATED ((ULONG)1228L)
#define ERROR_ALIAS_EXISTS ((ULONG)1379L)
#define ERROR_ALLOTTED_SPACE_EXCEEDED ((ULONG)1344L)
#define ERROR_AUDITING_DISABLED ((ULONG)0xC0090001)
#define ERROR_BAD_COMMAND ((ULONG)22L)
#define ERROR_BAD_DESCRIPTOR_FORMAT ((ULONG)1361L)
#define ERROR_BAD_DEV_TYPE ((ULONG)66L)
#define ERROR_BAD_IMPERSONATION_LEVEL ((ULONG)1346L)
#define ERROR_BAD_INHERITANCE_ACL ((ULONG)1340L)
#define ERROR_BAD_LOGON_SESSION_STATE ((ULONG)1365L)
#define ERROR_BAD_NETPATH ((ULONG)53L)
#define ERROR_BAD_NET_NAME ((ULONG)67L)
#define ERROR_BAD_NET_RESP ((ULONG)58L)
#define ERROR_BAD_PIPE ((ULONG)230L)
#define ERROR_BAD_REM_ADAP ((ULONG)60L)
#define ERROR_BAD_TOKEN_TYPE ((ULONG)1349L)
#define ERROR_BAD_VALIDATION_CLASS ((ULONG)1348L)
#define ERROR_BEGINNING_OF_MEDIA ((ULONG)1102L)
#define ERROR_BROKEN_PIPE ((ULONG)109L)
#define ERROR_BUSY ((ULONG)170L)
#define ERROR_BUS_RESET ((ULONG)1111L)
#define ERROR_CANT_ACCESS_DOMAIN_INFO ((ULONG)1351L)
#define ERROR_CANT_ACCESS_FILE ((ULONG)1920L)
#define ERROR_CANT_OPEN_ANONYMOUS ((ULONG)1347L)
#define ERROR_CANT_RESOLVE_FILENAME ((ULONG)1921L)
#define ERROR_CLEANER_CARTRIDGE_INSTALLED ((ULONG)4340L)
#define ERROR_CLUSTER_INVALID_NETWORK ((ULONG)5054L)
#define ERROR_CLUSTER_INVALID_NETWORK_PROVIDER ((ULONG)5049L)
#define ERROR_CLUSTER_INVALID_NODE ((ULONG)5039L)
#define ERROR_CLUSTER_INVALID_REQUEST ((ULONG)5048L)
#define ERROR_CLUSTER_JOIN_IN_PROGRESS ((ULONG)5041L)
#define ERROR_CLUSTER_JOIN_NOT_IN_PROGRESS ((ULONG)5053L)
#define ERROR_CLUSTER_LOCAL_NODE_NOT_FOUND ((ULONG)5043L)
#define ERROR_CLUSTER_NETINTERFACE_EXISTS ((ULONG)5046L)
#define ERROR_CLUSTER_NETINTERFACE_NOT_FOUND ((ULONG)5047L)
#define ERROR_CLUSTER_NETWORK_ALREADY_OFFLINE ((ULONG)5064L)
#define ERROR_CLUSTER_NETWORK_ALREADY_ONLINE ((ULONG)5063L)
#define ERROR_CLUSTER_NETWORK_EXISTS ((ULONG)5044L)
#define ERROR_CLUSTER_NETWORK_NOT_FOUND ((ULONG)5045L)
#define ERROR_CLUSTER_NETWORK_NOT_INTERNAL ((ULONG)5060L)
#define ERROR_CLUSTER_NODE_ALREADY_DOWN ((ULONG)5062L)
#define ERROR_CLUSTER_NODE_ALREADY_MEMBER ((ULONG)5065L)
#define ERROR_CLUSTER_NODE_ALREADY_UP ((ULONG)5061L)
#define ERROR_CLUSTER_NODE_DOWN ((ULONG)5050L)
#define ERROR_CLUSTER_NODE_EXISTS ((ULONG)5040L)
#define ERROR_CLUSTER_NODE_NOT_FOUND ((ULONG)5042L)
#define ERROR_CLUSTER_NODE_NOT_MEMBER ((ULONG)5052L)
#define ERROR_CLUSTER_NODE_NOT_PAUSED ((ULONG)5058L)
#define ERROR_CLUSTER_NODE_PAUSED ((ULONG)5070L)
#define ERROR_CLUSTER_NODE_UNREACHABLE ((ULONG)5051L)
#define ERROR_CLUSTER_NODE_UP ((ULONG)5056L)
#define ERROR_CLUSTER_NO_SECURITY_CONTEXT ((ULONG)5059L)
#define ERROR_CONNECTION_ABORTED ((ULONG)1236L)
#define ERROR_CONNECTION_ACTIVE ((ULONG)1230L)
#define ERROR_CONNECTION_COUNT_LIMIT ((ULONG)1238L)
#define ERROR_CONNECTION_INVALID ((ULONG)1229L)
#define ERROR_CONNECTION_REFUSED ((ULONG)1225L)
#define ERROR_CONTEXT_EXPIRED ((ULONG)1931L)
#define ERROR_COUNTER_TIMEOUT ((ULONG)1121L)
#define ERROR_CTX_BAD_VIDEO_MODE ((ULONG)7025L)
#define ERROR_CTX_CLIENT_LICENSE_IN_USE ((ULONG)7052L)
#define ERROR_CTX_CLIENT_LICENSE_NOT_SET ((ULONG)7053L)
#define ERROR_CTX_CLIENT_QUERY_TIMEOUT ((ULONG)7040L)
#define ERROR_CTX_CLOSE_PENDING ((ULONG)7007L)
#define ERROR_CTX_CONSOLE_CONNECT ((ULONG)7042L)
#define ERROR_CTX_CONSOLE_DISCONNECT ((ULONG)7041L)
#define ERROR_CTX_GRAPHICS_INVALID ((ULONG)7035L)
#define ERROR_CTX_INVALID_MODEMNAME ((ULONG)7010L)
#define ERROR_CTX_INVALID_PD ((ULONG)7002L)
#define ERROR_CTX_INVALID_WD ((ULONG)7049L)
#define ERROR_CTX_LICENSE_CLIENT_INVALID ((ULONG)7055L)
#define ERROR_CTX_LICENSE_EXPIRED ((ULONG)7056L)
#define ERROR_CTX_LICENSE_NOT_AVAILABLE ((ULONG)7054L)
#define ERROR_CTX_MODEM_INF_NOT_FOUND ((ULONG)7009L)
#define ERROR_CTX_MODEM_RESPONSE_BUSY ((ULONG)7015L)
#define ERROR_CTX_MODEM_RESPONSE_ERROR ((ULONG)7011L)
#define ERROR_CTX_MODEM_RESPONSE_NO_CARRIER ((ULONG)7013L)
#define ERROR_CTX_MODEM_RESPONSE_NO_DIALTONE ((ULONG)7014L)
#define ERROR_CTX_MODEM_RESPONSE_TIMEOUT ((ULONG)7012L)
#define ERROR_CTX_MODEM_RESPONSE_VOICE ((ULONG)7016L)
#define ERROR_CTX_NOT_CONSOLE ((ULONG)7038L)
#define ERROR_CTX_NO_OUTBUF ((ULONG)7008L)
#define ERROR_CTX_PD_NOT_FOUND ((ULONG)7003L)
#define ERROR_CTX_SHADOW_DENIED ((ULONG)7044L)
#define ERROR_CTX_SHADOW_DISABLED ((ULONG)7051L)
#define ERROR_CTX_SHADOW_ENDED_BY_MODE_CHANGE ((ULONG)7058L)
#define ERROR_CTX_SHADOW_INVALID ((ULONG)7050L)
#define ERROR_CTX_SHADOW_NOT_RUNNING ((ULONG)7057L)
#define ERROR_CTX_TD_ERROR ((ULONG)7017L)
#define ERROR_CTX_WD_NOT_FOUND ((ULONG)7004L)
#define ERROR_CTX_WINSTATION_ACCESS_DENIED ((ULONG)7045L)
#define ERROR_CTX_WINSTATION_ALREADY_EXISTS ((ULONG)7023L)
#define ERROR_CTX_WINSTATION_BUSY ((ULONG)7024L)
#define ERROR_CTX_WINSTATION_NAME_INVALID ((ULONG)7001L)
#define ERROR_CTX_WINSTATION_NOT_FOUND ((ULONG)7022L)
#define ERROR_DECRYPTION_FAILED ((ULONG)6001L)
#define ERROR_DESTINATION_ELEMENT_FULL ((ULONG)1161L)
#define ERROR_DEVICE_DOOR_OPEN ((ULONG)1166L)
#define ERROR_DEVICE_IN_USE ((ULONG)2404L)
#define ERROR_DEVICE_NOT_CONNECTED ((ULONG)1167L)
#define ERROR_DEVICE_NOT_PARTITIONED ((ULONG)1107L)
#define ERROR_DEVICE_REINITIALIZATION_NEEDED ((ULONG)1164L)
#define ERROR_DEVICE_REMOVED ((ULONG)1617L)
#define ERROR_DEVICE_REQUIRES_CLEANING ((ULONG)1165L)
#define ERROR_DEV_NOT_EXIST ((ULONG)55L)
#define ERROR_DIR_NOT_EMPTY ((ULONG)145L)
#define ERROR_DISK_OPERATION_FAILED ((ULONG)1127L)
#define ERROR_DISK_RECALIBRATE_FAILED ((ULONG)1126L)
#define ERROR_DISK_RESET_FAILED ((ULONG)1128L)
#define ERROR_DISK_TOO_FRAGMENTED ((ULONG)302L)
#define ERROR_DOMAIN_EXISTS ((ULONG)1356L)
#define ERROR_DOMAIN_LIMIT_EXCEEDED ((ULONG)1357L)
#define ERROR_DOMAIN_TRUST_INCONSISTENT ((ULONG)1810L)
#define ERROR_DRIVER_BLOCKED ((ULONG)1275L)
#define ERROR_DS_ADMIN_LIMIT_EXCEEDED ((ULONG)8228L)
#define ERROR_DS_AG_CANT_HAVE_UNIVERSAL_MEMBER ((ULONG)8578L)
#define ERROR_DS_ATTRIBUTE_OR_VALUE_EXISTS ((ULONG)8205L)
#define ERROR_DS_ATTRIBUTE_TYPE_UNDEFINED ((ULONG)8204L)
#define ERROR_DS_BUSY ((ULONG)8206L)
#define ERROR_DS_CANT_MOD_OBJ_CLASS ((ULONG)8215L)
#define ERROR_DS_CANT_MOD_PRIMARYGROUPID ((ULONG)8506L)
#define ERROR_DS_CANT_ON_NON_LEAF ((ULONG)8213L)
#define ERROR_DS_CANT_ON_RDN ((ULONG)8214L)
#define ERROR_DS_CANT_START ((ULONG)8531L)
#define ERROR_DS_CROSS_DOM_MOVE_ERROR ((ULONG)8216L)
#define ERROR_DS_DS_REQUIRED ((ULONG)8478L)
#define ERROR_DS_GC_NOT_AVAILABLE ((ULONG)8217L)
#define ERROR_DS_GC_REQUIRED ((ULONG)8547L)
#define ERROR_DS_GLOBAL_CANT_HAVE_CROSSDOMAIN_MEMBER ((ULONG)8519L)
#define ERROR_DS_GLOBAL_CANT_HAVE_LOCAL_MEMBER ((ULONG)8516L)
#define ERROR_DS_GLOBAL_CANT_HAVE_UNIVERSAL_MEMBER ((ULONG)8517L)
#define ERROR_DS_HAVE_PRIMARY_MEMBERS ((ULONG)8521L)
#define ERROR_DS_INCORRECT_ROLE_OWNER ((ULONG)8210L)
#define ERROR_DS_INIT_FAILURE ((ULONG)8532L)
#define ERROR_DS_INIT_FAILURE_CONSOLE ((ULONG)8561L)
#define ERROR_DS_INVALID_ATTRIBUTE_SYNTAX ((ULONG)8203L)
#define ERROR_DS_INVALID_GROUP_TYPE ((ULONG)8513L)
#define ERROR_DS_LOCAL_CANT_HAVE_CROSSDOMAIN_LOCAL_MEMBER ((ULONG)8520L)
#define ERROR_DS_LOCAL_MEMBER_OF_LOCAL_ONLY ((ULONG)8548L)
#define ERROR_DS_MACHINE_ACCOUNT_CREATED_PRENT4 ((ULONG)8572L)
#define ERROR_DS_MACHINE_ACCOUNT_QUOTA_EXCEEDED ((ULONG)8557L)
#define ERROR_DS_MEMBERSHIP_EVALUATED_LOCALLY ((ULONG)8201L)
#define ERROR_DS_NO_ATTRIBUTE_OR_VALUE ((ULONG)8202L)
#define ERROR_DS_NO_FPO_IN_UNIVERSAL_GROUPS ((ULONG)8549L)
#define ERROR_DS_NO_MORE_RIDS ((ULONG)8209L)
#define ERROR_DS_NO_NEST_GLOBALGROUP_IN_MIXEDDOMAIN ((ULONG)8514L)
#define ERROR_DS_NO_NEST_LOCALGROUP_IN_MIXEDDOMAIN ((ULONG)8515L)
#define ERROR_DS_NO_RIDS_ALLOCATED ((ULONG)8208L)
#define ERROR_DS_OBJ_CLASS_VIOLATION ((ULONG)8212L)
#define ERROR_DS_RIDMGR_INIT_ERROR ((ULONG)8211L)
#define ERROR_DS_SAM_INIT_FAILURE ((ULONG)8504L)
#define ERROR_DS_SAM_INIT_FAILURE_CONSOLE ((ULONG)8562L)
#define ERROR_DS_SAM_NEED_BOOTKEY_FLOPPY ((ULONG)8530L)
#define ERROR_DS_SAM_NEED_BOOTKEY_PASSWORD ((ULONG)8529L)
#define ERROR_DS_SENSITIVE_GROUP_VIOLATION ((ULONG)8505L)
#define ERROR_DS_SHUTTING_DOWN ((ULONG)8364L)
#define ERROR_DS_UNAVAILABLE ((ULONG)8207L)
#define ERROR_DS_UNIVERSAL_CANT_HAVE_LOCAL_MEMBER ((ULONG)8518L)
#define ERROR_DUP_NAME ((ULONG)52L)
#define ERROR_EAS_NOT_SUPPORTED ((ULONG)282L)
#define ERROR_EA_LIST_INCONSISTENT ((ULONG)255L)
#define ERROR_EFS_ALG_BLOB_TOO_BIG ((ULONG)6013L)
#define ERROR_ENCRYPTION_FAILED ((ULONG)6000L)
#define ERROR_END_OF_MEDIA ((ULONG)1100L)
#define ERROR_EOM_OVERFLOW ((ULONG)1129L)
#define ERROR_EVENTLOG_CANT_START ((ULONG)1501L)
#define ERROR_EVENTLOG_FILE_CHANGED ((ULONG)1503L)
#define ERROR_EVENTLOG_FILE_CORRUPT ((ULONG)1500L)
#define ERROR_FILEMARK_DETECTED ((ULONG)1101L)
#define ERROR_FILE_CORRUPT ((ULONG)1392L)
#define ERROR_FILE_ENCRYPTED ((ULONG)6002L)
#define ERROR_FILE_INVALID ((ULONG)1006L)
#define ERROR_FILE_NOT_ENCRYPTED ((ULONG)6007L)
#define ERROR_FILE_OFFLINE ((ULONG)4350L)
#define ERROR_FLOPPY_BAD_REGISTERS ((ULONG)1125L)
#define ERROR_FLOPPY_ID_MARK_NOT_FOUND ((ULONG)1122L)
#define ERROR_FLOPPY_UNKNOWN_ERROR ((ULONG)1124L)
#define ERROR_FLOPPY_WRONG_CYLINDER ((ULONG)1123L)
#define ERROR_FULLSCREEN_MODE ((ULONG)1007L)
#define ERROR_GENERIC_NOT_MAPPED ((ULONG)1360L)
#define ERROR_GRACEFUL_DISCONNECT ((ULONG)1226L)
#define ERROR_HOST_DOWN ((ULONG)1256L)
#define ERROR_HOST_UNREACHABLE ((ULONG)1232L)
#define ERROR_ILLEGAL_ELEMENT_ADDRESS ((ULONG)1162L)
#define ERROR_INTERNAL_DB_CORRUPTION ((ULONG)1358L)
#define ERROR_INTERNAL_DB_ERROR ((ULONG)1383L)
#define ERROR_INVALID_BLOCK_LENGTH ((ULONG)1106L)
#define ERROR_INVALID_DOMAIN_ROLE ((ULONG)1354L)
#define ERROR_INVALID_DOMAIN_STATE ((ULONG)1353L)
#define ERROR_INVALID_EA_NAME ((ULONG)254L)
#define ERROR_INVALID_GROUP_ATTRIBUTES ((ULONG)1345L)
#define ERROR_INVALID_ID_AUTHORITY ((ULONG)1343L)
#define ERROR_INVALID_LEVEL ((ULONG)124L)
#define ERROR_INVALID_LOGON_TYPE ((ULONG)1367L)
#define ERROR_INVALID_MEMBER ((ULONG)1388L)
#define ERROR_INVALID_NETNAME ((ULONG)1214L)
#define ERROR_INVALID_OPLOCK_PROTOCOL ((ULONG)301L)
#define ERROR_INVALID_REPARSE_DATA ((ULONG)4392L)
#define ERROR_INVALID_SERVER_STATE ((ULONG)1352L)
#define ERROR_INVALID_USER_BUFFER ((ULONG)1784L)
#define ERROR_IRQ_BUSY ((ULONG)1119L)
#define ERROR_JOURNAL_DELETE_IN_PROGRESS ((ULONG)1178L)
#define ERROR_JOURNAL_ENTRY_DELETED ((ULONG)1181L)
#define ERROR_JOURNAL_NOT_ACTIVE ((ULONG)1179L)
#define ERROR_LICENSE_QUOTA_EXCEEDED ((ULONG)1395L)
#define ERROR_LM_CROSS_ENCRYPTION_REQUIRED ((ULONG)1390L)
#define ERROR_LOCAL_USER_SESSION_KEY ((ULONG)1303L)
#define ERROR_LOGIN_TIME_RESTRICTION ((ULONG)1239L)
#define ERROR_LOGIN_WKSTA_RESTRICTION ((ULONG)1240L)
#define ERROR_LOGON_NOT_GRANTED ((ULONG)1380L)
#define ERROR_LOGON_SESSION_COLLISION ((ULONG)1366L)
#define ERROR_LOGON_SESSION_EXISTS ((ULONG)1363L)
#define ERROR_LOGON_TYPE_NOT_GRANTED ((ULONG)1385L)
#define ERROR_LOG_FILE_FULL ((ULONG)1502L)
#define ERROR_MAGAZINE_NOT_PRESENT ((ULONG)1163L)
#define ERROR_MAPPED_ALIGNMENT ((ULONG)1132L)
#define ERROR_MEDIA_CHANGED ((ULONG)1110L)
#define ERROR_MEMBERS_PRIMARY_GROUP ((ULONG)1374L)
#define ERROR_MEMBER_IN_ALIAS ((ULONG)1378L)
#define ERROR_MEMBER_NOT_IN_ALIAS ((ULONG)1377L)
#define ERROR_MORE_WRITES ((ULONG)1120L)
#define ERROR_MR_MID_NOT_FOUND ((ULONG)317L)
#define ERROR_NETLOGON_NOT_STARTED ((ULONG)1792L)
#define ERROR_NETNAME_DELETED ((ULONG)64L)
#define ERROR_NETWORK_ACCESS_DENIED ((ULONG)65L)
#define ERROR_NETWORK_BUSY ((ULONG)54L)
#define ERROR_NETWORK_UNREACHABLE ((ULONG)1231L)
#define ERROR_NET_WRITE_FAULT ((ULONG)88L)
#define ERROR_NOLOGON_INTERDOMAIN_TRUST_ACCOUNT ((ULONG)1807L)
#define ERROR_NOLOGON_SERVER_TRUST_ACCOUNT ((ULONG)1809L)
#define ERROR_NOLOGON_WORKSTATION_TRUST_ACCOUNT ((ULONG)1808L)
#define ERROR_NOT_ALL_ASSIGNED ((ULONG)1300L)
#define ERROR_NOT_A_REPARSE_POINT ((ULONG)4390L)
#define ERROR_NOT_ENOUGH_SERVER_MEMORY ((ULONG)1130L)
#define ERROR_NOT_EXPORT_FORMAT ((ULONG)6008L)
#define ERROR_NOT_FOUND ((ULONG)1168L)
#define ERROR_NOT_LOGON_PROCESS ((ULONG)1362L)
#define ERROR_NOT_SAME_DEVICE ((ULONG)17L)
#define ERROR_NOT_SUPPORTED_ON_SBS ((ULONG)1254L)
#define ERROR_NO_BROWSER_SERVERS_FOUND ((ULONG)6118L)
#define ERROR_NO_DATA ((ULONG)232L)
#define ERROR_NO_DATA_DETECTED ((ULONG)1104L)
#define ERROR_NO_MATCH ((ULONG)1169L)
#define ERROR_NO_QUOTAS_FOR_ACCOUNT ((ULONG)1302L)
#define ERROR_NO_SECURITY_ON_OBJECT ((ULONG)1350L)
#define ERROR_NO_SPOOL_SPACE ((ULONG)62L)
#define ERROR_NO_SUCH_ALIAS ((ULONG)1376L)
#define ERROR_NO_SUCH_DOMAIN ((ULONG)1355L)
#define ERROR_NO_SUCH_MEMBER ((ULONG)1387L)
#define ERROR_NO_SUCH_PACKAGE ((ULONG)1364L)
#define ERROR_NO_SYSTEM_RESOURCES ((ULONG)1450L)
#define ERROR_NO_TOKEN ((ULONG)1008L)
#define ERROR_NO_TRACKING_SERVICE ((ULONG)1172L)
#define ERROR_NO_TRUST_LSA_SECRET ((ULONG)1786L)
#define ERROR_NO_TRUST_SAM_ACCOUNT ((ULONG)1787L)
#define ERROR_NO_UNICODE_TRANSLATION ((ULONG)1113L)
#define ERROR_NO_USER_SESSION_KEY ((ULONG)1394L)
#define ERROR_NT_CROSS_ENCRYPTION_REQUIRED ((ULONG)1386L)
#define ERROR_NULL_LM_PASSWORD ((ULONG)1304L)
#define ERROR_ONLY_IF_CONNECTED ((ULONG)1251L)
#define ERROR_OPEN_FILES ((ULONG)2401L)
#define ERROR_OPERATION_ABORTED ((ULONG)995L)
#define ERROR_OPLOCK_NOT_GRANTED ((ULONG)300L)
#define ERROR_OUT_OF_PAPER ((ULONG)28L)
#define ERROR_PARTITION_FAILURE ((ULONG)1105L)
#define ERROR_PIPE_BUSY ((ULONG)231L)
#define ERROR_PIPE_CONNECTED ((ULONG)535L)
#define ERROR_PIPE_LISTENING ((ULONG)536L)
#define ERROR_PIPE_NOT_CONNECTED ((ULONG)233L)
#define ERROR_POLICY_OBJECT_NOT_FOUND ((ULONG)8219L)
#define ERROR_POLICY_ONLY_IN_DS ((ULONG)8220L)
#define ERROR_PORT_UNREACHABLE ((ULONG)1234L)
#define ERROR_POSSIBLE_DEADLOCK ((ULONG)1131L)
#define ERROR_PRINTQ_FULL ((ULONG)61L)
#define ERROR_PRINT_CANCELLED ((ULONG)63L)
#define ERROR_PROTOCOL_UNREACHABLE ((ULONG)1233L)
#define ERROR_REDIR_PAUSED ((ULONG)72L)
#define ERROR_REG_NAT_CONSUMPTION ((ULONG)1261L)
#define ERROR_REMOTE_SESSION_LIMIT_EXCEEDED ((ULONG)1220L)
#define ERROR_REMOTE_STORAGE_MEDIA_ERROR ((ULONG)4352L)
#define ERROR_REMOTE_STORAGE_NOT_ACTIVE ((ULONG)4351L)
#define ERROR_REM_NOT_LIST ((ULONG)51L)
#define ERROR_REPARSE_ATTRIBUTE_CONFLICT ((ULONG)4391L)
#define ERROR_REPARSE_TAG_INVALID ((ULONG)4393L)
#define ERROR_REPARSE_TAG_MISMATCH ((ULONG)4394L)
#define ERROR_REQUEST_ABORTED ((ULONG)1235L)
#define ERROR_REQ_NOT_ACCEP ((ULONG)71L)
#define ERROR_RETRY ((ULONG)1237L)
#define ERROR_RXACT_COMMIT_FAILURE ((ULONG)1370L)
#define ERROR_RXACT_INVALID_STATE ((ULONG)1369L)
#define ERROR_SAM_INIT_FAILURE ((ULONG)8541L)
#define ERROR_SECRET_TOO_LONG ((ULONG)1382L)
#define ERROR_SEM_TIMEOUT ((ULONG)121L)
#define ERROR_SERIAL_NO_DEVICE ((ULONG)1118L)
#define ERROR_SERVER_DISABLED ((ULONG)1341L)
#define ERROR_SERVER_NOT_DISABLED ((ULONG)1342L)
#define ERROR_SERVER_SHUTDOWN_IN_PROGRESS ((ULONG)1255L)
#define ERROR_SERVICE_ALREADY_RUNNING ((ULONG)1056L)
#define ERROR_SERVICE_DISABLED ((ULONG)1058L)
#define ERROR_SESSION_CREDENTIAL_CONFLICT ((ULONG)1219L)
#define ERROR_SETMARK_DETECTED ((ULONG)1103L)
#define ERROR_SET_NOT_FOUND ((ULONG)1170L)
#define ERROR_SHARED_POLICY ((ULONG)8218L)
#define ERROR_SHARING_PAUSED ((ULONG)70L)
#define ERROR_SHUTDOWN_IN_PROGRESS ((ULONG)1115L)
#define ERROR_SOME_NOT_MAPPED ((ULONG)1301L)
#define ERROR_SOURCE_ELEMENT_EMPTY ((ULONG)1160L)
#define ERROR_SPECIAL_ACCOUNT ((ULONG)1371L)
#define ERROR_SPECIAL_USER ((ULONG)1373L)
#define ERROR_SXS_ACTIVATION_CONTEXT_DISABLED ((ULONG)14006L)
#define ERROR_SXS_ASSEMBLY_NOT_FOUND ((ULONG)14003L)
#define ERROR_SXS_CANT_GEN_ACTCTX ((ULONG)14001L)
#define ERROR_SXS_INVALID_ACTCTXDATA_FORMAT ((ULONG)14002L)
#define ERROR_SXS_KEY_NOT_FOUND ((ULONG)14007L)
#define ERROR_SXS_MANIFEST_FORMAT_ERROR ((ULONG)14004L)
#define ERROR_SXS_MANIFEST_PARSE_ERROR ((ULONG)14005L)
#define ERROR_SXS_PROCESS_DEFAULT_ALREADY_SET ((ULONG)14011L)
#define ERROR_SXS_SECTION_NOT_FOUND ((ULONG)14000L)
#define ERROR_SXS_THREAD_QUERIES_DISABLED ((ULONG)14010L)
#define ERROR_SXS_WRONG_SECTION_TYPE ((ULONG)14009L)
#define ERROR_TOKEN_ALREADY_IN_USE ((ULONG)1375L)
#define ERROR_TOO_MANY_CMDS ((ULONG)56L)
#define ERROR_TOO_MANY_CONTEXT_IDS ((ULONG)1384L)
#define ERROR_TOO_MANY_NAMES ((ULONG)68L)
#define ERROR_TOO_MANY_OPEN_FILES ((ULONG)4L)
#define ERROR_TOO_MANY_SECRETS ((ULONG)1381L)
#define ERROR_TOO_MANY_SESS ((ULONG)69L)
#define ERROR_TOO_MANY_SIDS ((ULONG)1389L)
#define ERROR_TRANSPORT_FULL ((ULONG)4328L)
#define ERROR_TRUSTED_DOMAIN_FAILURE ((ULONG)1788L)
#define ERROR_TRUSTED_RELATIONSHIP_FAILURE ((ULONG)1789L)
#define ERROR_TRUST_FAILURE ((ULONG)1790L)
#define ERROR_UNABLE_TO_LOCK_MEDIA ((ULONG)1108L)
#define ERROR_UNABLE_TO_UNLOAD_MEDIA ((ULONG)1109L)
#define ERROR_UNEXP_NET_ERR ((ULONG)59L)
#define ERROR_UNRECOGNIZED_MEDIA ((ULONG)1785L)
#define ERROR_UNRECOGNIZED_VOLUME ((ULONG)1005L)
#define ERROR_USER_MAPPED_FILE ((ULONG)1224L)
#define ERROR_VC_DISCONNECTED ((ULONG)240L)
#define ERROR_WMI_ALREADY_DISABLED ((ULONG)4212L)
#define ERROR_WMI_ALREADY_ENABLED ((ULONG)4206L)
#define ERROR_WMI_GUID_DISCONNECTED ((ULONG)4207L)
#define ERROR_WMI_GUID_NOT_FOUND ((ULONG)4200L)
#define ERROR_WMI_INSTANCE_NOT_FOUND ((ULONG)4201L)
#define ERROR_WMI_ITEMID_NOT_FOUND ((ULONG)4202L)
#define ERROR_WMI_READ_ONLY ((ULONG)4213L)
#define ERROR_WMI_SET_FAILURE ((ULONG)4214L)
#define ERROR_WMI_TRY_AGAIN ((ULONG)4203L)
#define ERROR_WRITE_PROTECT ((ULONG)19L)
#define ERROR_WRONG_TARGET_NAME ((ULONG)1396L)
#define NTE_BAD_KEYSET ((ULONG)0x80090016)
#define NTE_NO_KEY ((ULONG)0x8009000D)
#define RPC_NT_ADDRESS_ERROR ((NTSTATUS)0xC0020045)
#define RPC_NT_ALREADY_LISTENING ((NTSTATUS)0xC002000E)
#define RPC_NT_ALREADY_REGISTERED ((NTSTATUS)0xC002000C)
#define RPC_NT_BAD_STUB_DATA ((NTSTATUS)0xC003000C)
#define RPC_NT_BINDING_HAS_NO_AUTH ((NTSTATUS)0xC002002F)
#define RPC_NT_BINDING_INCOMPLETE ((NTSTATUS)0xC0020051)
#define RPC_NT_BYTE_COUNT_TOO_SMALL ((NTSTATUS)0xC003000B)
#define RPC_NT_CALL_CANCELLED ((NTSTATUS)0xC0020050)
#define RPC_NT_CALL_FAILED ((NTSTATUS)0xC002001B)
#define RPC_NT_CALL_FAILED_DNE ((NTSTATUS)0xC002001C)
#define RPC_NT_CALL_IN_PROGRESS ((NTSTATUS)0xC0020049)
#define RPC_NT_CANNOT_SUPPORT ((NTSTATUS)0xC0020041)
#define RPC_NT_CANT_CREATE_ENDPOINT ((NTSTATUS)0xC0020015)
#define RPC_NT_COMM_FAILURE ((NTSTATUS)0xC0020052)
#define RPC_NT_DUPLICATE_ENDPOINT ((NTSTATUS)0xC0020029)
#define RPC_NT_ENTRY_ALREADY_EXISTS ((NTSTATUS)0xC002003D)
#define RPC_NT_ENTRY_NOT_FOUND ((NTSTATUS)0xC002003E)
#define RPC_NT_ENUM_VALUE_OUT_OF_RANGE ((NTSTATUS)0xC003000A)
#define RPC_NT_FP_DIV_ZERO ((NTSTATUS)0xC0020046)
#define RPC_NT_FP_OVERFLOW ((NTSTATUS)0xC0020048)
#define RPC_NT_FP_UNDERFLOW ((NTSTATUS)0xC0020047)
#define RPC_NT_GROUP_MEMBER_NOT_FOUND ((NTSTATUS)0xC002004B)
#define RPC_NT_INCOMPLETE_NAME ((NTSTATUS)0xC0020038)
#define RPC_NT_INTERFACE_NOT_FOUND ((NTSTATUS)0xC002003C)
#define RPC_NT_INTERNAL_ERROR ((NTSTATUS)0xC0020043)
#define RPC_NT_INVALID_ASYNC_CALL ((NTSTATUS)0xC0020063)
#define RPC_NT_INVALID_ASYNC_HANDLE ((NTSTATUS)0xC0020062)
#define RPC_NT_INVALID_AUTH_IDENTITY ((NTSTATUS)0xC0020032)
#define RPC_NT_INVALID_BINDING ((NTSTATUS)0xC0020003)
#define RPC_NT_INVALID_BOUND ((NTSTATUS)0xC0020023)
#define RPC_NT_INVALID_ENDPOINT_FORMAT ((NTSTATUS)0xC0020007)
#define RPC_NT_INVALID_ES_ACTION ((NTSTATUS)0xC0030059)
#define RPC_NT_INVALID_NAF_ID ((NTSTATUS)0xC0020040)
#define RPC_NT_INVALID_NAME_SYNTAX ((NTSTATUS)0xC0020025)
#define RPC_NT_INVALID_NETWORK_OPTIONS ((NTSTATUS)0xC0020019)
#define RPC_NT_INVALID_NET_ADDR ((NTSTATUS)0xC0020008)
#define RPC_NT_INVALID_OBJECT ((NTSTATUS)0xC002004D)
#define RPC_NT_INVALID_PIPE_OBJECT ((NTSTATUS)0xC003005C)
#define RPC_NT_INVALID_PIPE_OPERATION ((NTSTATUS)0xC003005D)
#define RPC_NT_INVALID_RPC_PROTSEQ ((NTSTATUS)0xC0020005)
#define RPC_NT_INVALID_STRING_BINDING ((NTSTATUS)0xC0020001)
#define RPC_NT_INVALID_STRING_UUID ((NTSTATUS)0xC0020006)
#define RPC_NT_INVALID_TAG ((NTSTATUS)0xC0020022)
#define RPC_NT_INVALID_TIMEOUT ((NTSTATUS)0xC002000A)
#define RPC_NT_INVALID_VERS_OPTION ((NTSTATUS)0xC0020039)
#define RPC_NT_MAX_CALLS_TOO_SMALL ((NTSTATUS)0xC002002B)
#define RPC_NT_NAME_SERVICE_UNAVAILABLE ((NTSTATUS)0xC002003F)
#define RPC_NT_NOTHING_TO_EXPORT ((NTSTATUS)0xC0020037)
#define RPC_NT_NOT_ALL_OBJS_UNEXPORTED ((NTSTATUS)0xC002003B)
#define RPC_NT_NOT_CANCELLED ((NTSTATUS)0xC0020058)
#define RPC_NT_NOT_LISTENING ((NTSTATUS)0xC0020010)
#define RPC_NT_NOT_RPC_ERROR ((NTSTATUS)0xC0020055)
#define RPC_NT_NO_BINDINGS ((NTSTATUS)0xC0020013)
#define RPC_NT_NO_CALL_ACTIVE ((NTSTATUS)0xC002001A)
#define RPC_NT_NO_CONTEXT_AVAILABLE ((NTSTATUS)0xC0020042)
#define RPC_NT_NO_ENDPOINT_FOUND ((NTSTATUS)0xC0020009)
#define RPC_NT_NO_ENTRY_NAME ((NTSTATUS)0xC0020024)
#define RPC_NT_NO_INTERFACES ((NTSTATUS)0xC002004F)
#define RPC_NT_NO_MORE_BINDINGS ((NTSTATUS)0xC002004A)
#define RPC_NT_NO_MORE_ENTRIES ((NTSTATUS)0xC0030001)
#define RPC_NT_NO_MORE_MEMBERS ((NTSTATUS)0xC002003A)
#define RPC_NT_NO_PRINC_NAME ((NTSTATUS)0xC0020054)
#define RPC_NT_NO_PROTSEQS ((NTSTATUS)0xC0020014)
#define RPC_NT_NO_PROTSEQS_REGISTERED ((NTSTATUS)0xC002000F)
#define RPC_NT_NULL_REF_POINTER ((NTSTATUS)0xC0030009)
#define RPC_NT_OBJECT_NOT_FOUND ((NTSTATUS)0xC002000B)
#define RPC_NT_OUT_OF_RESOURCES ((NTSTATUS)0xC0020016)
#define RPC_NT_PIPE_CLOSED ((NTSTATUS)0xC003005F)
#define RPC_NT_PIPE_DISCIPLINE_ERROR ((NTSTATUS)0xC0030060)
#define RPC_NT_PIPE_EMPTY ((NTSTATUS)0xC0030061)
#define RPC_NT_PROCNUM_OUT_OF_RANGE ((NTSTATUS)0xC002002E)
#define RPC_NT_PROTOCOL_ERROR ((NTSTATUS)0xC002001D)
#define RPC_NT_PROTSEQ_NOT_FOUND ((NTSTATUS)0xC002002D)
#define RPC_NT_PROTSEQ_NOT_SUPPORTED ((NTSTATUS)0xC0020004)
#define RPC_NT_SEC_PKG_ERROR ((NTSTATUS)0xC0020057)
#define RPC_NT_SEND_INCOMPLETE ((NTSTATUS)0x400200AF)
#define RPC_NT_SERVER_TOO_BUSY ((NTSTATUS)0xC0020018)
#define RPC_NT_SERVER_UNAVAILABLE ((NTSTATUS)0xC0020017)
#define RPC_NT_SS_CANNOT_GET_CALL_HANDLE ((NTSTATUS)0xC0030008)
#define RPC_NT_SS_CHAR_TRANS_OPEN_FAIL ((NTSTATUS)0xC0030002)
#define RPC_NT_SS_CHAR_TRANS_SHORT_FILE ((NTSTATUS)0xC0030003)
#define RPC_NT_SS_CONTEXT_DAMAGED ((NTSTATUS)0xC0030006)
#define RPC_NT_SS_CONTEXT_MISMATCH ((NTSTATUS)0xC0030005)
#define RPC_NT_SS_HANDLES_MISMATCH ((NTSTATUS)0xC0030007)
#define RPC_NT_SS_IN_NULL_CONTEXT ((NTSTATUS)0xC0030004)
#define RPC_NT_STRING_TOO_LONG ((NTSTATUS)0xC002002C)
#define RPC_NT_TYPE_ALREADY_REGISTERED ((NTSTATUS)0xC002000D)
#define RPC_NT_UNKNOWN_AUTHN_LEVEL ((NTSTATUS)0xC0020031)
#define RPC_NT_UNKNOWN_AUTHN_SERVICE ((NTSTATUS)0xC0020030)
#define RPC_NT_UNKNOWN_AUTHN_TYPE ((NTSTATUS)0xC002002A)
#define RPC_NT_UNKNOWN_AUTHZ_SERVICE ((NTSTATUS)0xC0020033)
#define RPC_NT_UNKNOWN_IF ((NTSTATUS)0xC0020012)
#define RPC_NT_UNKNOWN_MGR_TYPE ((NTSTATUS)0xC0020011)
#define RPC_NT_UNSUPPORTED_AUTHN_LEVEL ((NTSTATUS)0xC0020053)
#define RPC_NT_UNSUPPORTED_NAME_SYNTAX ((NTSTATUS)0xC0020026)
#define RPC_NT_UNSUPPORTED_TRANS_SYN ((NTSTATUS)0xC002001F)
#define RPC_NT_UNSUPPORTED_TYPE ((NTSTATUS)0xC0020021)
#define RPC_NT_UUID_LOCAL_ONLY ((NTSTATUS)0x40020056)
#define RPC_NT_UUID_NO_ADDRESS ((NTSTATUS)0xC0020028)
#define RPC_NT_WRONG_ES_VERSION ((NTSTATUS)0xC003005A)
#define RPC_NT_WRONG_KIND_OF_BINDING ((NTSTATUS)0xC0020002)
#define RPC_NT_WRONG_PIPE_VERSION ((NTSTATUS)0xC003005E)
#define RPC_NT_WRONG_STUB_VERSION ((NTSTATUS)0xC003005B)
#define RPC_NT_ZERO_DIVIDE ((NTSTATUS)0xC0020044)
#define STATUS_ACCESS_DISABLED_BY_POLICY_DEFAULT ((NTSTATUS)0xC0000361)
#define STATUS_ACCESS_DISABLED_BY_POLICY_OTHER ((NTSTATUS)0xC0000364)
#define STATUS_ACCESS_DISABLED_BY_POLICY_PATH ((NTSTATUS)0xC0000362)
#define STATUS_ACCESS_DISABLED_BY_POLICY_PUBLISHER ((NTSTATUS)0xC0000363)
#define STATUS_ACCOUNT_EXPIRED ((NTSTATUS)0xC0000193)
#define STATUS_ADAPTER_HARDWARE_ERROR ((NTSTATUS)0xC00000C2)
#define STATUS_ADDRESS_ALREADY_ASSOCIATED ((NTSTATUS)0xC0000238)
#define STATUS_ADDRESS_ALREADY_EXISTS ((NTSTATUS)0xC000020A)
#define STATUS_ADDRESS_CLOSED ((NTSTATUS)0xC000020B)
#define STATUS_ADDRESS_NOT_ASSOCIATED ((NTSTATUS)0xC0000239)
#define STATUS_ALIAS_EXISTS ((NTSTATUS)0xC0000154)
#define STATUS_ALLOTTED_SPACE_EXCEEDED ((NTSTATUS)0xC0000099)
#define STATUS_ALREADY_DISCONNECTED ((NTSTATUS)0x80000025)
#define STATUS_AUDITING_DISABLED ((NTSTATUS)0xC0000356)
#define STATUS_BAD_BINDINGS ((NTSTATUS)0xC000035B)
#define STATUS_BAD_DESCRIPTOR_FORMAT ((NTSTATUS)0xC00000E7)
#define STATUS_BAD_DEVICE_TYPE ((NTSTATUS)0xC00000CB)
#define STATUS_BAD_IMPERSONATION_LEVEL ((NTSTATUS)0xC00000A5)
#define STATUS_BAD_INHERITANCE_ACL ((NTSTATUS)0xC000007D)
#define STATUS_BAD_LOGON_SESSION_STATE ((NTSTATUS)0xC0000104)
#define STATUS_BAD_MASTER_BOOT_RECORD ((NTSTATUS)0xC00000A9)
#define STATUS_BAD_NETWORK_NAME ((NTSTATUS)0xC00000CC)
#define STATUS_BAD_NETWORK_PATH ((NTSTATUS)0xC00000BE)
#define STATUS_BAD_REMOTE_ADAPTER ((NTSTATUS)0xC00000C5)
#define STATUS_BAD_TOKEN_TYPE ((NTSTATUS)0xC00000A8)
#define STATUS_BAD_VALIDATION_CLASS ((NTSTATUS)0xC00000A7)
#define STATUS_BEGINNING_OF_MEDIA ((NTSTATUS)0x8000001F)
#define STATUS_BUS_RESET ((NTSTATUS)0x8000001D)
#define STATUS_CANCELLED ((NTSTATUS)0xC0000120)
#define STATUS_CANT_ACCESS_DOMAIN_INFO ((NTSTATUS)0xC00000DA)
#define STATUS_CANT_OPEN_ANONYMOUS ((NTSTATUS)0xC00000A6)
#define STATUS_CLEANER_CARTRIDGE_INSTALLED ((NTSTATUS)0x80000027)
#define STATUS_CLUSTER_INVALID_NETWORK ((NTSTATUS)0xC0130010)
#define STATUS_CLUSTER_INVALID_NETWORK_PROVIDER ((NTSTATUS)0xC013000B)
#define STATUS_CLUSTER_INVALID_NODE ((NTSTATUS)0xC0130001)
#define STATUS_CLUSTER_INVALID_REQUEST ((NTSTATUS)0xC013000A)
#define STATUS_CLUSTER_JOIN_IN_PROGRESS ((NTSTATUS)0xC0130003)
#define STATUS_CLUSTER_JOIN_NOT_IN_PROGRESS ((NTSTATUS)0xC013000F)
#define STATUS_CLUSTER_LOCAL_NODE_NOT_FOUND ((NTSTATUS)0xC0130005)
#define STATUS_CLUSTER_NETINTERFACE_EXISTS ((NTSTATUS)0xC0130008)
#define STATUS_CLUSTER_NETINTERFACE_NOT_FOUND ((NTSTATUS)0xC0130009)
#define STATUS_CLUSTER_NETWORK_ALREADY_OFFLINE ((NTSTATUS)0x80130004)
#define STATUS_CLUSTER_NETWORK_ALREADY_ONLINE ((NTSTATUS)0x80130003)
#define STATUS_CLUSTER_NETWORK_EXISTS ((NTSTATUS)0xC0130006)
#define STATUS_CLUSTER_NETWORK_NOT_FOUND ((NTSTATUS)0xC0130007)
#define STATUS_CLUSTER_NETWORK_NOT_INTERNAL ((NTSTATUS)0xC0130016)
#define STATUS_CLUSTER_NODE_ALREADY_DOWN ((NTSTATUS)0x80130002)
#define STATUS_CLUSTER_NODE_ALREADY_MEMBER ((NTSTATUS)0x80130005)
#define STATUS_CLUSTER_NODE_ALREADY_UP ((NTSTATUS)0x80130001)
#define STATUS_CLUSTER_NODE_DOWN ((NTSTATUS)0xC013000C)
#define STATUS_CLUSTER_NODE_EXISTS ((NTSTATUS)0xC0130002)
#define STATUS_CLUSTER_NODE_NOT_FOUND ((NTSTATUS)0xC0130004)
#define STATUS_CLUSTER_NODE_NOT_MEMBER ((NTSTATUS)0xC013000E)
#define STATUS_CLUSTER_NODE_NOT_PAUSED ((NTSTATUS)0xC0130014)
#define STATUS_CLUSTER_NODE_PAUSED ((NTSTATUS)0xC0130013)
#define STATUS_CLUSTER_NODE_UNREACHABLE ((NTSTATUS)0xC013000D)
#define STATUS_CLUSTER_NODE_UP ((NTSTATUS)0xC0130012)
#define STATUS_CLUSTER_NO_SECURITY_CONTEXT ((NTSTATUS)0xC0130015)
#define STATUS_CONNECTION_ABORTED ((NTSTATUS)0xC0000241)
#define STATUS_CONNECTION_ACTIVE ((NTSTATUS)0xC000023B)
#define STATUS_CONNECTION_COUNT_LIMIT ((NTSTATUS)0xC0000246)
#define STATUS_CONNECTION_DISCONNECTED ((NTSTATUS)0xC000020C)
#define STATUS_CONNECTION_INVALID ((NTSTATUS)0xC000023A)
#define STATUS_CONNECTION_IN_USE ((NTSTATUS)0xC0000108)
#define STATUS_CONNECTION_REFUSED ((NTSTATUS)0xC0000236)
#define STATUS_CONNECTION_RESET ((NTSTATUS)0xC000020D)
#define STATUS_COPY_PROTECTION_FAILURE ((NTSTATUS)0xC0000305)
#define STATUS_CRYPTO_SYSTEM_INVALID ((NTSTATUS)0xC00002F3)
#define STATUS_CSS_AUTHENTICATION_FAILURE ((NTSTATUS)0xC0000306)
#define STATUS_CSS_KEY_NOT_ESTABLISHED ((NTSTATUS)0xC0000308)
#define STATUS_CSS_KEY_NOT_PRESENT ((NTSTATUS)0xC0000307)
#define STATUS_CSS_REGION_MISMATCH ((NTSTATUS)0xC000030A)
#define STATUS_CSS_RESETS_EXHAUSTED ((NTSTATUS)0xC000030B)
#define STATUS_CSS_SCRAMBLED_SECTOR ((NTSTATUS)0xC0000309)
#define STATUS_CTX_BAD_VIDEO_MODE ((NTSTATUS)0xC00A0018)
#define STATUS_CTX_CLIENT_LICENSE_IN_USE ((NTSTATUS)0xC00A0034)
#define STATUS_CTX_CLIENT_LICENSE_NOT_SET ((NTSTATUS)0xC00A0033)
#define STATUS_CTX_CLIENT_QUERY_TIMEOUT ((NTSTATUS)0xC00A0026)
#define STATUS_CTX_CLOSE_PENDING ((NTSTATUS)0xC00A0006)
#define STATUS_CTX_CONSOLE_CONNECT ((NTSTATUS)0xC00A0028)
#define STATUS_CTX_CONSOLE_DISCONNECT ((NTSTATUS)0xC00A0027)
#define STATUS_CTX_GRAPHICS_INVALID ((NTSTATUS)0xC00A0022)
#define STATUS_CTX_INVALID_MODEMNAME ((NTSTATUS)0xC00A0009)
#define STATUS_CTX_INVALID_PD ((NTSTATUS)0xC00A0002)
#define STATUS_CTX_INVALID_WD ((NTSTATUS)0xC00A002E)
#define STATUS_CTX_LICENSE_CLIENT_INVALID ((NTSTATUS)0xC00A0012)
#define STATUS_CTX_LICENSE_EXPIRED ((NTSTATUS)0xC00A0014)
#define STATUS_CTX_LICENSE_NOT_AVAILABLE ((NTSTATUS)0xC00A0013)
#define STATUS_CTX_MODEM_INF_NOT_FOUND ((NTSTATUS)0xC00A0008)
#define STATUS_CTX_MODEM_RESPONSE_BUSY ((NTSTATUS)0xC00A000E)
#define STATUS_CTX_MODEM_RESPONSE_NO_CARRIER ((NTSTATUS)0xC00A000C)
#define STATUS_CTX_MODEM_RESPONSE_NO_DIALTONE ((NTSTATUS)0xC00A000D)
#define STATUS_CTX_MODEM_RESPONSE_TIMEOUT ((NTSTATUS)0xC00A000B)
#define STATUS_CTX_MODEM_RESPONSE_VOICE ((NTSTATUS)0xC00A000F)
#define STATUS_CTX_NOT_CONSOLE ((NTSTATUS)0xC00A0024)
#define STATUS_CTX_NO_OUTBUF ((NTSTATUS)0xC00A0007)
#define STATUS_CTX_PD_NOT_FOUND ((NTSTATUS)0xC00A0003)
#define STATUS_CTX_RESPONSE_ERROR ((NTSTATUS)0xC00A000A)
#define STATUS_CTX_SHADOW_DENIED ((NTSTATUS)0xC00A002A)
#define STATUS_CTX_SHADOW_DISABLED ((NTSTATUS)0xC00A0031)
#define STATUS_CTX_SHADOW_ENDED_BY_MODE_CHANGE ((NTSTATUS)0xC00A0035)
#define STATUS_CTX_SHADOW_INVALID ((NTSTATUS)0xC00A0030)
#define STATUS_CTX_SHADOW_NOT_RUNNING ((NTSTATUS)0xC00A0036)
#define STATUS_CTX_TD_ERROR ((NTSTATUS)0xC00A0010)
#define STATUS_CTX_WD_NOT_FOUND ((NTSTATUS)0xC00A002F)
#define STATUS_CTX_WINSTATION_ACCESS_DENIED ((NTSTATUS)0xC00A002B)
#define STATUS_CTX_WINSTATION_BUSY ((NTSTATUS)0xC00A0017)
#define STATUS_CTX_WINSTATION_NAME_COLLISION ((NTSTATUS)0xC00A0016)
#define STATUS_CTX_WINSTATION_NAME_INVALID ((NTSTATUS)0xC00A0001)
#define STATUS_CTX_WINSTATION_NOT_FOUND ((NTSTATUS)0xC00A0015)
#define STATUS_DECRYPTION_FAILED ((NTSTATUS)0xC000028B)
#define STATUS_DESTINATION_ELEMENT_FULL ((NTSTATUS)0xC0000284)
#define STATUS_DEVICE_BUSY ((NTSTATUS)0x80000011)
#define STATUS_DEVICE_CONFIGURATION_ERROR ((NTSTATUS)0xC0000182)
#define STATUS_DEVICE_DATA_ERROR ((NTSTATUS)0xC000009C)
#define STATUS_DEVICE_DOES_NOT_EXIST ((NTSTATUS)0xC00000C0)
#define STATUS_DEVICE_DOOR_OPEN ((NTSTATUS)0x80000289)
#define STATUS_DEVICE_NOT_CONNECTED ((NTSTATUS)0xC000009D)
#define STATUS_DEVICE_NOT_PARTITIONED ((NTSTATUS)0xC0000174)
#define STATUS_DEVICE_NOT_READY ((NTSTATUS)0xC00000A3)
#define STATUS_DEVICE_OFF_LINE ((NTSTATUS)0x80000010)
#define STATUS_DEVICE_PAPER_EMPTY ((NTSTATUS)0x8000000E)
#define STATUS_DEVICE_POWERED_OFF ((NTSTATUS)0x8000000F)
#define STATUS_DEVICE_POWER_FAILURE ((NTSTATUS)0xC000009E)
#define STATUS_DEVICE_PROTOCOL_ERROR ((NTSTATUS)0xC0000186)
#define STATUS_DEVICE_REMOVED ((NTSTATUS)0xC00002B6)
#define STATUS_DEVICE_REQUIRES_CLEANING ((NTSTATUS)0x80000288)
#define STATUS_DIRECTORY_IS_A_REPARSE_POINT ((NTSTATUS)0xC0000281)
#define STATUS_DIRECTORY_NOT_EMPTY ((NTSTATUS)0xC0000101)
#define STATUS_DIRECTORY_SERVICE_REQUIRED ((NTSTATUS)0xC00002B1)
#define STATUS_DISK_OPERATION_FAILED ((NTSTATUS)0xC000016A)
#define STATUS_DISK_RECALIBRATE_FAILED ((NTSTATUS)0xC0000169)
#define STATUS_DISK_RESET_FAILED ((NTSTATUS)0xC000016B)
#define STATUS_DOMAIN_EXISTS ((NTSTATUS)0xC00000E0)
#define STATUS_DOMAIN_LIMIT_EXCEEDED ((NTSTATUS)0xC00000E1)
#define STATUS_DOMAIN_TRUST_INCONSISTENT ((NTSTATUS)0xC000019B)
#define STATUS_DRIVER_BLOCKED ((NTSTATUS)0xC000036C)
#define STATUS_DRIVER_BLOCKED_CRITICAL ((NTSTATUS)0xC000036B)
#define STATUS_DRIVER_INTERNAL_ERROR ((NTSTATUS)0xC0000183)
#define STATUS_DS_ADMIN_LIMIT_EXCEEDED ((NTSTATUS)0xC00002C1)
#define STATUS_DS_AG_CANT_HAVE_UNIVERSAL_MEMBER ((NTSTATUS)0xC0000358)
#define STATUS_DS_ATTRIBUTE_OR_VALUE_EXISTS ((NTSTATUS)0xC00002A4)
#define STATUS_DS_ATTRIBUTE_TYPE_UNDEFINED ((NTSTATUS)0xC00002A3)
#define STATUS_DS_BUSY ((NTSTATUS)0xC00002A5)
#define STATUS_DS_CANT_MOD_OBJ_CLASS ((NTSTATUS)0xC00002AE)
#define STATUS_DS_CANT_MOD_PRIMARYGROUPID ((NTSTATUS)0xC00002D0)
#define STATUS_DS_CANT_ON_NON_LEAF ((NTSTATUS)0xC00002AC)
#define STATUS_DS_CANT_ON_RDN ((NTSTATUS)0xC00002AD)
#define STATUS_DS_CANT_START ((NTSTATUS)0xC00002E1)
#define STATUS_DS_CROSS_DOM_MOVE_FAILED ((NTSTATUS)0xC00002AF)
#define STATUS_DS_GC_NOT_AVAILABLE ((NTSTATUS)0xC00002B0)
#define STATUS_DS_GC_REQUIRED ((NTSTATUS)0xC00002E4)
#define STATUS_DS_GLOBAL_CANT_HAVE_CROSSDOMAIN_MEMBER ((NTSTATUS)0xC00002DA)
#define STATUS_DS_GLOBAL_CANT_HAVE_LOCAL_MEMBER ((NTSTATUS)0xC00002D7)
#define STATUS_DS_GLOBAL_CANT_HAVE_UNIVERSAL_MEMBER ((NTSTATUS)0xC00002D8)
#define STATUS_DS_HAVE_PRIMARY_MEMBERS ((NTSTATUS)0xC00002DC)
#define STATUS_DS_INCORRECT_ROLE_OWNER ((NTSTATUS)0xC00002A9)
#define STATUS_DS_INIT_FAILURE ((NTSTATUS)0xC00002E2)
#define STATUS_DS_INIT_FAILURE_CONSOLE ((NTSTATUS)0xC00002EC)
#define STATUS_DS_INVALID_ATTRIBUTE_SYNTAX ((NTSTATUS)0xC00002A2)
#define STATUS_DS_INVALID_GROUP_TYPE ((NTSTATUS)0xC00002D4)
#define STATUS_DS_LOCAL_CANT_HAVE_CROSSDOMAIN_LOCAL_MEMBER ((NTSTATUS)0xC00002DB)
#define STATUS_DS_LOCAL_MEMBER_OF_LOCAL_ONLY ((NTSTATUS)0xC00002E5)
#define STATUS_DS_MACHINE_ACCOUNT_QUOTA_EXCEEDED ((NTSTATUS)0xC00002E7)
#define STATUS_DS_MEMBERSHIP_EVALUATED_LOCALLY ((NTSTATUS)0x00000121)
#define STATUS_DS_NO_ATTRIBUTE_OR_VALUE ((NTSTATUS)0xC00002A1)
#define STATUS_DS_NO_FPO_IN_UNIVERSAL_GROUPS ((NTSTATUS)0xC00002E6)
#define STATUS_DS_NO_MORE_RIDS ((NTSTATUS)0xC00002A8)
#define STATUS_DS_NO_NEST_GLOBALGROUP_IN_MIXEDDOMAIN ((NTSTATUS)0xC00002D5)
#define STATUS_DS_NO_NEST_LOCALGROUP_IN_MIXEDDOMAIN ((NTSTATUS)0xC00002D6)
#define STATUS_DS_NO_RIDS_ALLOCATED ((NTSTATUS)0xC00002A7)
#define STATUS_DS_OBJ_CLASS_VIOLATION ((NTSTATUS)0xC00002AB)
#define STATUS_DS_RIDMGR_INIT_ERROR ((NTSTATUS)0xC00002AA)
#define STATUS_DS_SAM_INIT_FAILURE ((NTSTATUS)0xC00002CB)
#define STATUS_DS_SAM_INIT_FAILURE_CONSOLE ((NTSTATUS)0xC00002ED)
#define STATUS_DS_SENSITIVE_GROUP_VIOLATION ((NTSTATUS)0xC00002CD)
#define STATUS_DS_SHUTTING_DOWN ((NTSTATUS)0x40000370)
#define STATUS_DS_UNAVAILABLE ((NTSTATUS)0xC00002A6)
#define STATUS_DS_UNIVERSAL_CANT_HAVE_LOCAL_MEMBER ((NTSTATUS)0xC00002D9)
#define STATUS_DUPLICATE_NAME ((NTSTATUS)0xC00000BD)
#define STATUS_DUPLICATE_OBJECTID ((NTSTATUS)0xC000022A)
#define STATUS_EAS_NOT_SUPPORTED ((NTSTATUS)0xC000004F)
#define STATUS_EA_CORRUPT_ERROR ((NTSTATUS)0xC0000053)
#define STATUS_EA_LIST_INCONSISTENT ((NTSTATUS)0x80000014)
#define STATUS_EA_TOO_LARGE ((NTSTATUS)0xC0000050)
#define STATUS_EFS_ALG_BLOB_TOO_BIG ((NTSTATUS)0xC0000352)
#define STATUS_ENCRYPTION_FAILED ((NTSTATUS)0xC000028A)
#define STATUS_END_OF_MEDIA ((NTSTATUS)0x8000001E)
#define STATUS_EOM_OVERFLOW ((NTSTATUS)0xC0000177)
#define STATUS_EVENTLOG_CANT_START ((NTSTATUS)0xC000018F)
#define STATUS_EVENTLOG_FILE_CHANGED ((NTSTATUS)0xC0000197)
#define STATUS_EVENTLOG_FILE_CORRUPT ((NTSTATUS)0xC000018E)
#define STATUS_FAIL_CHECK ((NTSTATUS)0xC0000229)
#define STATUS_FILEMARK_DETECTED ((NTSTATUS)0x8000001B)
#define STATUS_FILES_OPEN ((NTSTATUS)0xC0000107)
#define STATUS_FILE_CORRUPT_ERROR ((NTSTATUS)0xC0000102)
#define STATUS_FILE_ENCRYPTED ((NTSTATUS)0xC0000293)
#define STATUS_FILE_INVALID ((NTSTATUS)0xC0000098)
#define STATUS_FILE_IS_OFFLINE ((NTSTATUS)0xC0000267)
#define STATUS_FILE_NOT_ENCRYPTED ((NTSTATUS)0xC0000291)
#define STATUS_FLOPPY_BAD_REGISTERS ((NTSTATUS)0xC0000168)
#define STATUS_FLOPPY_ID_MARK_NOT_FOUND ((NTSTATUS)0xC0000165)
#define STATUS_FLOPPY_UNKNOWN_ERROR ((NTSTATUS)0xC0000167)
#define STATUS_FLOPPY_WRONG_CYLINDER ((NTSTATUS)0xC0000166)
#define STATUS_FREE_VM_NOT_AT_BASE ((NTSTATUS)0xC000009F)
#define STATUS_FULLSCREEN_MODE ((NTSTATUS)0xC0000159)
#define STATUS_GENERIC_NOT_MAPPED ((NTSTATUS)0xC00000E6)
#define STATUS_GRACEFUL_DISCONNECT ((NTSTATUS)0xC0000237)
#define STATUS_HOST_DOWN ((NTSTATUS)0xC0000350)
#define STATUS_HOST_UNREACHABLE ((NTSTATUS)0xC000023D)
#define STATUS_ILLEGAL_ELEMENT_ADDRESS ((NTSTATUS)0xC0000285)
#define STATUS_IMAGE_ALREADY_LOADED ((NTSTATUS)0xC000010E)
#define STATUS_IMAGE_CHECKSUM_MISMATCH ((NTSTATUS)0xC0000221)
#define STATUS_IMAGE_MP_UP_MISMATCH ((NTSTATUS)0xC0000249)
#define STATUS_INSTANCE_NOT_AVAILABLE ((NTSTATUS)0xC00000AB)
#define STATUS_INSUFFICIENT_RESOURCES ((NTSTATUS)0xC000009A)
#define STATUS_INSUFF_SERVER_RESOURCES ((NTSTATUS)0xC0000205)
#define STATUS_INTERNAL_DB_CORRUPTION ((NTSTATUS)0xC00000E4)
#define STATUS_INTERNAL_DB_ERROR ((NTSTATUS)0xC0000158)
#define STATUS_INTERNAL_ERROR ((NTSTATUS)0xC00000E5)
#define STATUS_INVALID_ADDRESS ((NTSTATUS)0xC0000141)
#define STATUS_INVALID_ADDRESS_COMPONENT ((NTSTATUS)0xC0000207)
#define STATUS_INVALID_ADDRESS_WILDCARD ((NTSTATUS)0xC0000208)
#define STATUS_INVALID_BLOCK_LENGTH ((NTSTATUS)0xC0000173)
#define STATUS_INVALID_BUFFER_SIZE ((NTSTATUS)0xC0000206)
#define STATUS_INVALID_CONNECTION ((NTSTATUS)0xC0000140)
#define STATUS_INVALID_DEVICE_STATE ((NTSTATUS)0xC0000184)
#define STATUS_INVALID_DOMAIN_ROLE ((NTSTATUS)0xC00000DE)
#define STATUS_INVALID_DOMAIN_STATE ((NTSTATUS)0xC00000DD)
#define STATUS_INVALID_EA_FLAG ((NTSTATUS)0x80000015)
#define STATUS_INVALID_EA_NAME ((NTSTATUS)0x80000013)
#define STATUS_INVALID_GROUP_ATTRIBUTES ((NTSTATUS)0xC00000A4)
#define STATUS_INVALID_ID_AUTHORITY ((NTSTATUS)0xC0000084)
#define STATUS_INVALID_IMAGE_FORMAT ((NTSTATUS)0xC000007B)
#define STATUS_INVALID_IMAGE_LE_FORMAT ((NTSTATUS)0xC000012E)
#define STATUS_INVALID_IMAGE_NE_FORMAT ((NTSTATUS)0xC000011B)
#define STATUS_INVALID_IMAGE_NOT_MZ ((NTSTATUS)0xC000012F)
#define STATUS_INVALID_IMAGE_PROTECT ((NTSTATUS)0xC0000130)
#define STATUS_INVALID_IMAGE_WIN_16 ((NTSTATUS)0xC0000131)
#define STATUS_INVALID_IMAGE_WIN_32 ((NTSTATUS)0xC0000359)
#define STATUS_INVALID_IMAGE_WIN_64 ((NTSTATUS)0xC000035A)
#define STATUS_INVALID_LEVEL ((NTSTATUS)0xC0000148)
#define STATUS_INVALID_LOGON_TYPE ((NTSTATUS)0xC000010B)
#define STATUS_INVALID_MEMBER ((NTSTATUS)0xC000017B)
#define STATUS_INVALID_NETWORK_RESPONSE ((NTSTATUS)0xC00000C3)
#define STATUS_INVALID_OPLOCK_PROTOCOL ((NTSTATUS)0xC00000E3)
#define STATUS_INVALID_PIPE_STATE ((NTSTATUS)0xC00000AD)
#define STATUS_INVALID_READ_MODE ((NTSTATUS)0xC00000B4)
#define STATUS_INVALID_SERVER_STATE ((NTSTATUS)0xC00000DC)
#define STATUS_INVALID_USER_BUFFER ((NTSTATUS)0xC00000E8)
#define STATUS_IO_DEVICE_ERROR ((NTSTATUS)0xC0000185)
#define STATUS_IO_REPARSE_DATA_INVALID ((NTSTATUS)0xC0000278)
#define STATUS_IO_REPARSE_TAG_INVALID ((NTSTATUS)0xC0000276)
#define STATUS_IO_REPARSE_TAG_MISMATCH ((NTSTATUS)0xC0000277)
#define STATUS_IO_REPARSE_TAG_NOT_HANDLED ((NTSTATUS)0xC0000279)
#define STATUS_IO_TIMEOUT ((NTSTATUS)0xC00000B5)
#define STATUS_JOURNAL_DELETE_IN_PROGRESS ((NTSTATUS)0xC00002B7)
#define STATUS_JOURNAL_ENTRY_DELETED ((NTSTATUS)0xC00002CF)
#define STATUS_JOURNAL_NOT_ACTIVE ((NTSTATUS)0xC00002B8)
#define STATUS_KDC_INVALID_REQUEST ((NTSTATUS)0xC00002FB)
#define STATUS_KDC_UNABLE_TO_REFER ((NTSTATUS)0xC00002FC)
#define STATUS_KDC_UNKNOWN_ETYPE ((NTSTATUS)0xC00002FD)
#define STATUS_LICENSE_QUOTA_EXCEEDED ((NTSTATUS)0xC0000259)
#define STATUS_LICENSE_VIOLATION ((NTSTATUS)0xC000026A)
#define STATUS_LINK_FAILED ((NTSTATUS)0xC000013E)
#define STATUS_LINK_TIMEOUT ((NTSTATUS)0xC000013F)
#define STATUS_LM_CROSS_ENCRYPTION_REQUIRED ((NTSTATUS)0xC000017F)
#define STATUS_LOCAL_DISCONNECT ((NTSTATUS)0xC000013B)
#define STATUS_LOCAL_USER_SESSION_KEY ((NTSTATUS)0x40000006)
#define STATUS_LOGIN_TIME_RESTRICTION ((NTSTATUS)0xC0000247)
#define STATUS_LOGIN_WKSTA_RESTRICTION ((NTSTATUS)0xC0000248)
#define STATUS_LOGON_NOT_GRANTED ((NTSTATUS)0xC0000155)
#define STATUS_LOGON_SESSION_COLLISION ((NTSTATUS)0xC0000105)
#define STATUS_LOGON_SESSION_EXISTS ((NTSTATUS)0xC00000EE)
#define STATUS_LOGON_TYPE_NOT_GRANTED ((NTSTATUS)0xC000015B)
#define STATUS_LOG_FILE_FULL ((NTSTATUS)0xC0000188)
#define STATUS_MAGAZINE_NOT_PRESENT ((NTSTATUS)0xC0000286)
#define STATUS_MAPPED_ALIGNMENT ((NTSTATUS)0xC0000220)
#define STATUS_MAPPED_FILE_SIZE_ZERO ((NTSTATUS)0xC000011E)
#define STATUS_MAX_REFERRALS_EXCEEDED ((NTSTATUS)0xC00002F4)
#define STATUS_MEDIA_CHANGED ((NTSTATUS)0x8000001C)
#define STATUS_MEDIA_WRITE_PROTECTED ((NTSTATUS)0xC00000A2)
#define STATUS_MEMBERS_PRIMARY_GROUP ((NTSTATUS)0xC0000127)
#define STATUS_MEMBER_IN_ALIAS ((NTSTATUS)0xC0000153)
#define STATUS_MEMBER_NOT_IN_ALIAS ((NTSTATUS)0xC0000152)
#define STATUS_MEMORY_NOT_ALLOCATED ((NTSTATUS)0xC00000A0)
#define STATUS_MESSAGE_NOT_FOUND ((NTSTATUS)0xC0000109)
#define STATUS_MFT_TOO_FRAGMENTED ((NTSTATUS)0xC0000304)
#define STATUS_MUST_BE_KDC ((NTSTATUS)0xC00002F5)
#define STATUS_NETLOGON_NOT_STARTED ((NTSTATUS)0xC0000192)
#define STATUS_NETWORK_ACCESS_DENIED ((NTSTATUS)0xC00000CA)
#define STATUS_NETWORK_BUSY ((NTSTATUS)0xC00000BF)
#define STATUS_NETWORK_CREDENTIAL_CONFLICT ((NTSTATUS)0xC0000195)
#define STATUS_NETWORK_NAME_DELETED ((NTSTATUS)0xC00000C9)
#define STATUS_NETWORK_SESSION_EXPIRED ((NTSTATUS)0xC000035C)
#define STATUS_NETWORK_UNREACHABLE ((NTSTATUS)0xC000023C)
#define STATUS_NET_WRITE_FAULT ((NTSTATUS)0xC00000D2)
#define STATUS_NOLOGON_INTERDOMAIN_TRUST_ACCOUNT ((NTSTATUS)0xC0000198)
#define STATUS_NOLOGON_SERVER_TRUST_ACCOUNT ((NTSTATUS)0xC000019A)
#define STATUS_NOLOGON_WORKSTATION_TRUST_ACCOUNT ((NTSTATUS)0xC0000199)
#define STATUS_NONEXISTENT_EA_ENTRY ((NTSTATUS)0xC0000051)
#define STATUS_NOT_ALL_ASSIGNED ((NTSTATUS)0x00000106)
#define STATUS_NOT_A_REPARSE_POINT ((NTSTATUS)0xC0000275)
#define STATUS_NOT_CLIENT_SESSION ((NTSTATUS)0xC0000217)
#define STATUS_NOT_EXPORT_FORMAT ((NTSTATUS)0xC0000292)
#define STATUS_NOT_FOUND ((NTSTATUS)0xC0000225)
#define STATUS_NOT_LOGON_PROCESS ((NTSTATUS)0xC00000ED)
#define STATUS_NOT_SAME_DEVICE ((NTSTATUS)0xC00000D4)
#define STATUS_NOT_SERVER_SESSION ((NTSTATUS)0xC0000216)
#define STATUS_NOT_SUPPORTED_ON_SBS ((NTSTATUS)0xC0000300)
#define STATUS_NO_BROWSER_SERVERS_FOUND ((NTSTATUS)0xC000021C)
#define STATUS_NO_DATA_DETECTED ((NTSTATUS)0x80000022)
#define STATUS_NO_EAS_ON_FILE ((NTSTATUS)0xC0000052)
#define STATUS_NO_EFS ((NTSTATUS)0xC000028E)
#define STATUS_NO_IP_ADDRESSES ((NTSTATUS)0xC00002F1)
#define STATUS_NO_PA_DATA ((NTSTATUS)0xC00002F8)
#define STATUS_NO_QUOTAS_FOR_ACCOUNT ((NTSTATUS)0x0000010D)
#define STATUS_NO_RECOVERY_POLICY ((NTSTATUS)0xC000028D)
#define STATUS_NO_SECURITY_ON_OBJECT ((NTSTATUS)0xC00000D7)
#define STATUS_NO_SPOOL_SPACE ((NTSTATUS)0xC00000C7)
#define STATUS_NO_SUCH_ALIAS ((NTSTATUS)0xC0000151)
#define STATUS_NO_SUCH_DOMAIN ((NTSTATUS)0xC00000DF)
#define STATUS_NO_SUCH_MEMBER ((NTSTATUS)0xC000017A)
#define STATUS_NO_SUCH_PACKAGE ((NTSTATUS)0xC00000FE)
#define STATUS_NO_TGT_REPLY ((NTSTATUS)0xC00002EF)
#define STATUS_NO_TOKEN ((NTSTATUS)0xC000007C)
#define STATUS_NO_TRACKING_SERVICE ((NTSTATUS)0xC000029F)
#define STATUS_NO_TRUST_LSA_SECRET ((NTSTATUS)0xC000018A)
#define STATUS_NO_TRUST_SAM_ACCOUNT ((NTSTATUS)0xC000018B)
#define STATUS_NO_USER_KEYS ((NTSTATUS)0xC0000290)
#define STATUS_NO_USER_SESSION_KEY ((NTSTATUS)0xC0000202)
#define STATUS_NT_CROSS_ENCRYPTION_REQUIRED ((NTSTATUS)0xC000015D)
#define STATUS_NULL_LM_PASSWORD ((NTSTATUS)0x4000000D)
#define STATUS_OBJECTID_EXISTS ((NTSTATUS)0xC000022B)
#define STATUS_OBJECTID_NOT_FOUND ((NTSTATUS)0xC00002F0)
#define STATUS_ONLY_IF_CONNECTED ((NTSTATUS)0xC00002CC)
#define STATUS_OPLOCK_NOT_GRANTED ((NTSTATUS)0xC00000E2)
#define STATUS_PARTITION_FAILURE ((NTSTATUS)0xC0000172)
#define STATUS_PATH_NOT_COVERED ((NTSTATUS)0xC0000257)
#define STATUS_PIPE_BROKEN ((NTSTATUS)0xC000014B)
#define STATUS_PIPE_BUSY ((NTSTATUS)0xC00000AE)
#define STATUS_PIPE_CLOSING ((NTSTATUS)0xC00000B1)
#define STATUS_PIPE_CONNECTED ((NTSTATUS)0xC00000B2)
#define STATUS_PIPE_DISCONNECTED ((NTSTATUS)0xC00000B0)
#define STATUS_PIPE_EMPTY ((NTSTATUS)0xC00000D9)
#define STATUS_PIPE_LISTENING ((NTSTATUS)0xC00000B3)
#define STATUS_PIPE_NOT_AVAILABLE ((NTSTATUS)0xC00000AC)
#define STATUS_PKINIT_NAME_MISMATCH ((NTSTATUS)0xC00002F9)
#define STATUS_PLUGPLAY_NO_DEVICE ((NTSTATUS)0xC000025E)
#define STATUS_POLICY_OBJECT_NOT_FOUND ((NTSTATUS)0xC000029A)
#define STATUS_POLICY_ONLY_IN_DS ((NTSTATUS)0xC000029B)
#define STATUS_PORT_UNREACHABLE ((NTSTATUS)0xC000023F)
#define STATUS_POSSIBLE_DEADLOCK ((NTSTATUS)0xC0000194)
#define STATUS_PRENT4_MACHINE_ACCOUNT ((NTSTATUS)0xC0000357)
#define STATUS_PRINT_CANCELLED ((NTSTATUS)0xC00000C8)
#define STATUS_PRINT_QUEUE_FULL ((NTSTATUS)0xC00000C6)
#define STATUS_PROPSET_NOT_FOUND ((NTSTATUS)0xC0000230)
#define STATUS_PROTOCOL_UNREACHABLE ((NTSTATUS)0xC000023E)
#define STATUS_RANGE_NOT_LOCKED ((NTSTATUS)0xC000007E)
#define STATUS_REDIRECTOR_NOT_STARTED ((NTSTATUS)0xC00000FB)
#define STATUS_REDIRECTOR_PAUSED ((NTSTATUS)0xC00000D1)
#define STATUS_REDIRECTOR_STARTED ((NTSTATUS)0xC00000FC)
#define STATUS_REG_NAT_CONSUMPTION ((NTSTATUS)0xC00002C9)
#define STATUS_REINITIALIZATION_NEEDED ((NTSTATUS)0xC0000287)
#define STATUS_REMOTE_DISCONNECT ((NTSTATUS)0xC000013C)
#define STATUS_REMOTE_NOT_LISTENING ((NTSTATUS)0xC00000BC)
#define STATUS_REMOTE_RESOURCES ((NTSTATUS)0xC000013D)
#define STATUS_REMOTE_SESSION_LIMIT ((NTSTATUS)0xC0000196)
#define STATUS_REMOTE_STORAGE_MEDIA_ERROR ((NTSTATUS)0xC000029E)
#define STATUS_REMOTE_STORAGE_NOT_ACTIVE ((NTSTATUS)0xC000029D)
#define STATUS_REPARSE_ATTRIBUTE_CONFLICT ((NTSTATUS)0xC00002B2)
#define STATUS_REPARSE_POINT_NOT_RESOLVED ((NTSTATUS)0xC0000280)
#define STATUS_REQUEST_ABORTED ((NTSTATUS)0xC0000240)
#define STATUS_REQUEST_NOT_ACCEPTED ((NTSTATUS)0xC00000D0)
#define STATUS_RESOURCE_NOT_OWNED ((NTSTATUS)0xC0000264)
#define STATUS_RETRY ((NTSTATUS)0xC000022D)
#define STATUS_RXACT_COMMIT_FAILURE ((NTSTATUS)0xC000011D)
#define STATUS_RXACT_INVALID_STATE ((NTSTATUS)0xC000011C)
#define STATUS_SAM_INIT_FAILURE ((NTSTATUS)0xC00002E3)
#define STATUS_SAM_NEED_BOOTKEY_FLOPPY ((NTSTATUS)0xC00002E0)
#define STATUS_SAM_NEED_BOOTKEY_PASSWORD ((NTSTATUS)0xC00002DF)
#define STATUS_SECRET_TOO_LONG ((NTSTATUS)0xC0000157)
#define STATUS_SERIAL_COUNTER_TIMEOUT ((NTSTATUS)0x4000000C)
#define STATUS_SERIAL_MORE_WRITES ((NTSTATUS)0x40000008)
#define STATUS_SERIAL_NO_DEVICE_INITED ((NTSTATUS)0xC0000150)
#define STATUS_SERVER_DISABLED ((NTSTATUS)0xC0000080)
#define STATUS_SERVER_NOT_DISABLED ((NTSTATUS)0xC0000081)
#define STATUS_SERVER_SHUTDOWN_IN_PROGRESS ((NTSTATUS)0xC00002FF)
#define STATUS_SETMARK_DETECTED ((NTSTATUS)0x80000021)
#define STATUS_SHARED_IRQ_BUSY ((NTSTATUS)0xC000016C)
#define STATUS_SHARED_POLICY ((NTSTATUS)0xC0000299)
#define STATUS_SHARING_PAUSED ((NTSTATUS)0xC00000CF)
#define STATUS_SHUTDOWN_IN_PROGRESS ((NTSTATUS)0xC00002FE)
#define STATUS_SMARTCARD_LOGON_REQUIRED ((NTSTATUS)0xC00002FA)
#define STATUS_SOME_NOT_MAPPED ((NTSTATUS)0x00000107)
#define STATUS_SOURCE_ELEMENT_EMPTY ((NTSTATUS)0xC0000283)
#define STATUS_SPECIAL_ACCOUNT ((NTSTATUS)0xC0000124)
#define STATUS_SPECIAL_USER ((NTSTATUS)0xC0000126)
#define STATUS_STRONG_CRYPTO_NOT_SUPPORTED ((NTSTATUS)0xC00002F6)
#define STATUS_SXS_ACTIVATION_CONTEXT_DISABLED ((NTSTATUS)0xC0150007)
#define STATUS_SXS_ASSEMBLY_NOT_FOUND ((NTSTATUS)0xC0150004)
#define STATUS_SXS_CANT_GEN_ACTCTX ((NTSTATUS)0xC0150002)
#define STATUS_SXS_INVALID_ACTCTXDATA_FORMAT ((NTSTATUS)0xC0150003)
#define STATUS_SXS_KEY_NOT_FOUND ((NTSTATUS)0xC0150008)
#define STATUS_SXS_MANIFEST_FORMAT_ERROR ((NTSTATUS)0xC0150005)
#define STATUS_SXS_MANIFEST_PARSE_ERROR ((NTSTATUS)0xC0150006)
#define STATUS_SXS_PROCESS_DEFAULT_ALREADY_SET ((NTSTATUS)0xC015000E)
#define STATUS_SXS_SECTION_NOT_FOUND ((NTSTATUS)0xC0150001)
#define STATUS_SXS_THREAD_QUERIES_DISABLED ((NTSTATUS)0xC015000B)
#define STATUS_SXS_WRONG_SECTION_TYPE ((NTSTATUS)0xC015000A)
#define STATUS_TOKEN_ALREADY_IN_USE ((NTSTATUS)0xC000012B)
#define STATUS_TOO_LATE ((NTSTATUS)0xC0000189)
#define STATUS_TOO_MANY_ADDRESSES ((NTSTATUS)0xC0000209)
#define STATUS_TOO_MANY_COMMANDS ((NTSTATUS)0xC00000C1)
#define STATUS_TOO_MANY_CONTEXT_IDS ((NTSTATUS)0xC000015A)
#define STATUS_TOO_MANY_GUIDS_REQUESTED ((NTSTATUS)0xC0000082)
#define STATUS_TOO_MANY_NAMES ((NTSTATUS)0xC00000CD)
#define STATUS_TOO_MANY_NODES ((NTSTATUS)0xC000020E)
#define STATUS_TOO_MANY_OPENED_FILES ((NTSTATUS)0xC000011F)
#define STATUS_TOO_MANY_PAGING_FILES ((NTSTATUS)0xC0000097)
#define STATUS_TOO_MANY_PRINCIPALS ((NTSTATUS)0xC00002F7)
#define STATUS_TOO_MANY_SECRETS ((NTSTATUS)0xC0000156)
#define STATUS_TOO_MANY_SESSIONS ((NTSTATUS)0xC00000CE)
#define STATUS_TOO_MANY_SIDS ((NTSTATUS)0xC000017E)
#define STATUS_TRANSACTION_ABORTED ((NTSTATUS)0xC000020F)
#define STATUS_TRANSACTION_INVALID_ID ((NTSTATUS)0xC0000214)
#define STATUS_TRANSACTION_INVALID_TYPE ((NTSTATUS)0xC0000215)
#define STATUS_TRANSACTION_NO_MATCH ((NTSTATUS)0xC0000212)
#define STATUS_TRANSACTION_NO_RELEASE ((NTSTATUS)0xC0000211)
#define STATUS_TRANSACTION_RESPONDED ((NTSTATUS)0xC0000213)
#define STATUS_TRANSACTION_TIMED_OUT ((NTSTATUS)0xC0000210)
#define STATUS_TRANSPORT_FULL ((NTSTATUS)0xC00002CA)
#define STATUS_TRUSTED_DOMAIN_FAILURE ((NTSTATUS)0xC000018C)
#define STATUS_TRUSTED_RELATIONSHIP_FAILURE ((NTSTATUS)0xC000018D)
#define STATUS_TRUST_FAILURE ((NTSTATUS)0xC0000190)
#define STATUS_UNABLE_TO_LOCK_MEDIA ((NTSTATUS)0xC0000175)
#define STATUS_UNABLE_TO_UNLOAD_MEDIA ((NTSTATUS)0xC0000176)
#define STATUS_UNEXPECTED_NETWORK_ERROR ((NTSTATUS)0xC00000C4)
#define STATUS_UNFINISHED_CONTEXT_DELETED ((NTSTATUS)0xC00002EE)
#define STATUS_UNMAPPABLE_CHARACTER ((NTSTATUS)0xC0000162)
#define STATUS_UNRECOGNIZED_MEDIA ((NTSTATUS)0xC0000014)
#define STATUS_UNRECOGNIZED_VOLUME ((NTSTATUS)0xC000014F)
#define STATUS_UNSUPPORTED_PREAUTH ((NTSTATUS)0xC0000351)
#define STATUS_USER_MAPPED_FILE ((NTSTATUS)0xC0000243)
#define STATUS_USER_SESSION_DELETED ((NTSTATUS)0xC0000203)
#define STATUS_VERIFY_REQUIRED ((NTSTATUS)0x80000016)
#define STATUS_VIRTUAL_CIRCUIT_CLOSED ((NTSTATUS)0xC00000D6)
#define STATUS_WMI_ALREADY_DISABLED ((NTSTATUS)0xC0000302)
#define STATUS_WMI_ALREADY_ENABLED ((NTSTATUS)0xC0000303)
#define STATUS_WMI_GUID_DISCONNECTED ((NTSTATUS)0xC0000301)
#define STATUS_WMI_GUID_NOT_FOUND ((NTSTATUS)0xC0000295)
#define STATUS_WMI_INSTANCE_NOT_FOUND ((NTSTATUS)0xC0000296)
#define STATUS_WMI_ITEMID_NOT_FOUND ((NTSTATUS)0xC0000297)
#define STATUS_WMI_NOT_SUPPORTED ((NTSTATUS)0xC00002DD)
#define STATUS_WMI_READ_ONLY ((NTSTATUS)0xC00002C6)
#define STATUS_WMI_SET_FAILURE ((NTSTATUS)0xC00002C7)
#define STATUS_WMI_TRY_AGAIN ((NTSTATUS)0xC0000298)
#define STATUS_WRONG_CREDENTIAL_HANDLE ((NTSTATUS)0xC00002F2)
#define STATUS_WRONG_EFS ((NTSTATUS)0xC000028F)

#define STATUS_ENDPOINT_CLOSED ((NTSTATUS)0xC0980001L)
#define STATUS_DISCONNECTED ((NTSTATUS)0xC0980002L)
#define STATUS_NONEXISTENT_NET_NAME ((NTSTATUS)0xC0980008L)

#define SEC_E_ALGORITHM_MISMATCH ((NTSTATUS)0x80090331)
#define SEC_E_BAD_BINDINGS ((NTSTATUS)0x80090346)
#define SEC_E_BAD_PKGID ((NTSTATUS)0x80090316)
#define SEC_E_BUFFER_TOO_SMALL ((NTSTATUS)0x80090321)
#define SEC_E_CANNOT_INSTALL ((NTSTATUS)0x80090307)
#define SEC_E_CANNOT_PACK ((NTSTATUS)0x80090309)
#define SEC_E_CERT_EXPIRED ((NTSTATUS)0x80090328)
#define SEC_E_CERT_UNKNOWN ((NTSTATUS)0x80090327)
#define SEC_E_CONTEXT_EXPIRED ((NTSTATUS)0x80090317)
#define SEC_E_CRYPTO_SYSTEM_INVALID ((NTSTATUS)0x80090337)
#define SEC_E_DECRYPT_FAILURE ((NTSTATUS)0x80090330)
#define SEC_E_ENCRYPT_FAILURE ((NTSTATUS)0x80090329)
#define SEC_E_ILLEGAL_MESSAGE ((NTSTATUS)0x80090326)
#define SEC_E_INCOMPLETE_CREDENTIALS ((NTSTATUS)0x80090320)
#define SEC_E_INCOMPLETE_MESSAGE ((NTSTATUS)0x80090318)
#define SEC_E_INSUFFICIENT_MEMORY ((NTSTATUS)0x80090300)
#define SEC_E_INTERNAL_ERROR ((NTSTATUS)0x80090304)
#define SEC_E_INVALID_HANDLE ((NTSTATUS)0x80090301)
#define SEC_E_INVALID_TOKEN ((NTSTATUS)0x80090308)
#define SEC_E_ISSUING_CA_UNTRUSTED ((NTSTATUS)1267L)
#define SEC_E_KDC_INVALID_REQUEST ((NTSTATUS)0x80090340)
#define SEC_E_KDC_UNABLE_TO_REFER ((NTSTATUS)0x80090341)
#define SEC_E_KDC_UNKNOWN_ETYPE ((NTSTATUS)0x80090342)
#define SEC_E_LOGON_DENIED ((NTSTATUS)0x8009030C)
#define SEC_E_MAX_REFERRALS_EXCEEDED ((NTSTATUS)0x80090338)
#define SEC_E_MESSAGE_ALTERED ((NTSTATUS)0x8009030F)
#define SEC_E_MULTIPLE_ACCOUNTS ((NTSTATUS)0x80090347)
#define SEC_E_MUST_BE_KDC ((NTSTATUS)0x80090339)
#define SEC_E_NOT_OWNER ((NTSTATUS)0x80090306)
#define SEC_E_NO_AUTHENTICATING_AUTHORITY ((NTSTATUS)0x80090311)
#define SEC_E_NO_CREDENTIALS ((NTSTATUS)0x8009030E)
#define SEC_E_NO_IMPERSONATION ((NTSTATUS)0x8009030B)
#define SEC_E_NO_IP_ADDRESSES ((NTSTATUS)0x80090335)
#define SEC_E_NO_KERB_KEY ((NTSTATUS)0x80090348)
#define SEC_E_NO_PA_DATA ((NTSTATUS)0x8009033C)
#define SEC_E_NO_TGT_REPLY ((NTSTATUS)0x80090334)
#define SEC_E_OUT_OF_SEQUENCE ((NTSTATUS)0x80090310)
#define SEC_E_PKINIT_CLIENT_FAILURE ((NTSTATUS)1269L)
#define SEC_E_PKINIT_NAME_MISMATCH ((NTSTATUS)0x8009033D)
#define SEC_E_QOP_NOT_SUPPORTED ((NTSTATUS)0x8009030A)
#define SEC_E_REVOCATION_OFFLINE_C ((NTSTATUS)1268L)
#define SEC_E_SECPKG_NOT_FOUND ((NTSTATUS)0x80090305)
#define SEC_E_SMARTCARD_CERT_EXPIRED ((NTSTATUS)1270L)
#define SEC_E_SMARTCARD_CERT_REVOKED ((NTSTATUS)1266L)
#define SEC_E_SMARTCARD_LOGON_REQUIRED ((NTSTATUS)0x8009033E)
#define SEC_E_STRONG_CRYPTO_NOT_SUPPORTED ((NTSTATUS)0x8009033A)
#define SEC_E_TARGET_UNKNOWN ((NTSTATUS)0x80090303)
#define SEC_E_TOO_MANY_PRINCIPALS ((NTSTATUS)0x8009033B)
#define SEC_E_UNFINISHED_CONTEXT_DELETED ((NTSTATUS)0x80090333)
#define SEC_E_UNKNOWN_CREDENTIALS ((NTSTATUS)0x8009030D)
#define SEC_E_UNSUPPORTED_FUNCTION ((NTSTATUS)0x80090302)
#define SEC_E_UNSUPPORTED_PREAUTH ((NTSTATUS)0x80090343)
#define SEC_E_UNTRUSTED_ROOT ((NTSTATUS)0x80090325)
#define SEC_E_WRONG_CREDENTIAL_HANDLE ((NTSTATUS)0x80090336)
#define SEC_E_WRONG_PRINCIPAL ((NTSTATUS)0x80090322)

#define SCARD_E_COMM_DATA_LOST ((ULONG)0x8010002F)
#define SCARD_E_NO_SMARTCARD ((ULONG)0x8010000C)
#define SCARD_E_NO_SUCH_CERTIFICATE ((ULONG)0x8010002C)
#define SCARD_W_CARD_NOT_AUTHENTICATED ((ULONG)0x8010006F)
#define SCARD_W_CHV_BLOCKED ((ULONG)0x8010006C)
#define SCARD_W_WRONG_CHV ((ULONG)0x8010006B)

#define NO_ERROR 0L
#define STG_E_CSS_AUTHENTICATION_FAILURE ((NTSTATUS)0x80030306)
#define STG_E_CSS_KEY_NOT_ESTABLISHED ((NTSTATUS)0x80030308)
#define STG_E_CSS_KEY_NOT_PRESENT ((NTSTATUS)0x80030307)
#define STG_E_CSS_REGION_MISMATCH ((NTSTATUS)0x8003030A)
#define STG_E_CSS_SCRAMBLED_SECTOR ((NTSTATUS)0x80030309)
#define STG_E_RESETS_EXHAUSTED ((NTSTATUS)0x8003030B)
#define STG_E_STATUS_COPY_PROTECTION_FAILURE ((NTSTATUS)0x80030305)

#define RPC_S_ADDRESS_ERROR ((ULONG)1768L)
#define RPC_S_ALREADY_LISTENING ((ULONG)1713L)
#define RPC_S_ALREADY_REGISTERED ((ULONG)1711L)
#define RPC_S_BINDING_HAS_NO_AUTH ((ULONG)1746L)
#define RPC_S_BINDING_INCOMPLETE ((ULONG)1819L)
#define RPC_S_CALL_CANCELLED ((ULONG)1818L)
#define RPC_S_CALL_FAILED ((ULONG)1726L)
#define RPC_S_CALL_FAILED_DNE ((ULONG)1727L)
#define RPC_S_CALL_IN_PROGRESS ((ULONG)1791L)
#define RPC_S_CANNOT_SUPPORT ((ULONG)1764L)
#define RPC_S_CANT_CREATE_ENDPOINT ((ULONG)1720L)
#define RPC_S_COMM_FAILURE ((ULONG)1820L)
#define RPC_S_DUPLICATE_ENDPOINT ((ULONG)1740L)
#define RPC_S_ENTRY_ALREADY_EXISTS ((ULONG)1760L)
#define RPC_S_ENTRY_NOT_FOUND ((ULONG)1761L)
#define RPC_S_FP_DIV_ZERO ((ULONG)1769L)
#define RPC_S_FP_OVERFLOW ((ULONG)1771L)
#define RPC_S_FP_UNDERFLOW ((ULONG)1770L)
#define RPC_S_GROUP_MEMBER_NOT_FOUND ((ULONG)1898L)
#define RPC_S_INCOMPLETE_NAME ((ULONG)1755L)
#define RPC_S_INTERFACE_NOT_FOUND ((ULONG)1759L)
#define RPC_S_INTERNAL_ERROR ((ULONG)1766L)
#define RPC_S_INVALID_ASYNC_CALL ((ULONG)1915L)
#define RPC_S_INVALID_ASYNC_HANDLE ((ULONG)1914L)
#define RPC_S_INVALID_AUTH_IDENTITY ((ULONG)1749L)
#define RPC_S_INVALID_BOUND ((ULONG)1734L)
#define RPC_S_INVALID_ENDPOINT_FORMAT ((ULONG)1706L)
#define RPC_S_INVALID_NAF_ID ((ULONG)1763L)
#define RPC_S_INVALID_NAME_SYNTAX ((ULONG)1736L)
#define RPC_S_INVALID_NETWORK_OPTIONS ((ULONG)1724L)
#define RPC_S_INVALID_NET_ADDR ((ULONG)1707L)
#define RPC_S_INVALID_OBJECT ((ULONG)1900L)
#define RPC_S_INVALID_RPC_PROTSEQ ((ULONG)1704L)
#define RPC_S_INVALID_STRING_BINDING ((ULONG)1700L)
#define RPC_S_INVALID_STRING_UUID ((ULONG)1705L)
#define RPC_S_INVALID_TAG ((ULONG)1733L)
#define RPC_S_INVALID_TIMEOUT ((ULONG)1709L)
#define RPC_S_INVALID_VERS_OPTION ((ULONG)1756L)
#define RPC_S_MAX_CALLS_TOO_SMALL ((ULONG)1742L)
#define RPC_S_NAME_SERVICE_UNAVAILABLE ((ULONG)1762L)
#define RPC_S_NOTHING_TO_EXPORT ((ULONG)1754L)
#define RPC_S_NOT_ALL_OBJS_UNEXPORTED ((ULONG)1758L)
#define RPC_S_NOT_CANCELLED ((ULONG)1826L)
#define RPC_S_NOT_LISTENING ((ULONG)1715L)
#define RPC_S_NOT_RPC_ERROR ((ULONG)1823L)
#define RPC_S_NO_BINDINGS ((ULONG)1718L)
#define RPC_S_NO_CALL_ACTIVE ((ULONG)1725L)
#define RPC_S_NO_CONTEXT_AVAILABLE ((ULONG)1765L)
#define RPC_S_NO_ENDPOINT_FOUND ((ULONG)1708L)
#define RPC_S_NO_ENTRY_NAME ((ULONG)1735L)
#define RPC_S_NO_INTERFACES ((ULONG)1817L)
#define RPC_S_NO_MORE_BINDINGS ((ULONG)1806L)
#define RPC_S_NO_MORE_MEMBERS ((ULONG)1757L)
#define RPC_S_NO_PRINC_NAME ((ULONG)1822L)
#define RPC_S_NO_PROTSEQS ((ULONG)1719L)
#define RPC_S_NO_PROTSEQS_REGISTERED ((ULONG)1714L)
#define RPC_S_OBJECT_NOT_FOUND ((ULONG)1710L)
#define RPC_S_OUT_OF_RESOURCES ((ULONG)1721L)
#define RPC_S_PROCNUM_OUT_OF_RANGE ((ULONG)1745L)
#define RPC_S_PROTOCOL_ERROR ((ULONG)1728L)
#define RPC_S_PROTSEQ_NOT_FOUND ((ULONG)1744L)
#define RPC_S_PROTSEQ_NOT_SUPPORTED ((ULONG)1703L)
#define RPC_S_SEC_PKG_ERROR ((ULONG)1825L)
#define RPC_S_SEND_INCOMPLETE ((ULONG)1913L)
#define RPC_S_SERVER_TOO_BUSY ((ULONG)1723L)
#define RPC_S_SERVER_UNAVAILABLE ((ULONG)1722L)
#define RPC_S_STRING_TOO_LONG ((ULONG)1743L)
#define RPC_S_TYPE_ALREADY_REGISTERED ((ULONG)1712L)
#define RPC_S_UNKNOWN_AUTHN_LEVEL ((ULONG)1748L)
#define RPC_S_UNKNOWN_AUTHN_SERVICE ((ULONG)1747L)
#define RPC_S_UNKNOWN_AUTHN_TYPE ((ULONG)1741L)
#define RPC_S_UNKNOWN_AUTHZ_SERVICE ((ULONG)1750L)
#define RPC_S_UNKNOWN_IF ((ULONG)1717L)
#define RPC_S_UNKNOWN_MGR_TYPE ((ULONG)1716L)
#define RPC_S_UNSUPPORTED_AUTHN_LEVEL ((ULONG)1821L)
#define RPC_S_UNSUPPORTED_NAME_SYNTAX ((ULONG)1737L)
#define RPC_S_UNSUPPORTED_TRANS_SYN ((ULONG)1730L)
#define RPC_S_UNSUPPORTED_TYPE ((ULONG)1732L)
#define RPC_S_UUID_LOCAL_ONLY ((ULONG)1824L)
#define RPC_S_UUID_NO_ADDRESS ((ULONG)1739L)
#define RPC_S_WRONG_KIND_OF_BINDING ((ULONG)1701L)
#define RPC_S_ZERO_DIVIDE ((ULONG)1767L)

#define CRYPT_E_NO_REVOCATION_CHECK ((NTSTATUS)0x80092012)
#define CRYPT_E_REVOCATION_OFFLINE ((NTSTATUS)0x80092013)
#define CRYPT_E_REVOKED ((NTSTATUS)0x80092010)
#define EPT_NT_CANT_CREATE ((NTSTATUS)0xC002004C)
#define EPT_NT_CANT_PERFORM_OP ((NTSTATUS)0xC0020035)
#define EPT_NT_INVALID_ENTRY ((NTSTATUS)0xC0020034)
#define EPT_NT_NOT_REGISTERED ((NTSTATUS)0xC0020036)
#define EPT_S_CANT_CREATE ((NTSTATUS)1899L)
#define EPT_S_CANT_PERFORM_OP ((NTSTATUS)1752L)
#define EPT_S_INVALID_ENTRY ((NTSTATUS)1751L)
#define EPT_S_NOT_REGISTERED ((NTSTATUS)1753L)
#define TRUST_E_CERT_SIGNATURE ((NTSTATUS)0x80096004)

#define RPC_X_BAD_STUB_DATA ((NTSTATUS)270)
#define RPC_X_BYTE_COUNT_TOO_SMALL ((NTSTATUS)269)
#define RPC_X_ENUM_VALUE_OUT_OF_RANGE ((NTSTATUS)268)
#define RPC_X_INVALID_ES_ACTION ((NTSTATUS)271)
#define RPC_X_INVALID_PIPE_OBJECT ((NTSTATUS)275)
#define RPC_X_INVALID_PIPE_OPERATION ((NTSTATUS)276)
#define RPC_X_NO_MORE_ENTRIES ((NTSTATUS)256)
#define RPC_X_NULL_REF_POINTER ((NTSTATUS)267)
#define RPC_X_PIPE_CLOSED ((NTSTATUS)278)
#define RPC_X_PIPE_DISCIPLINE_ERROR ((NTSTATUS)281)
#define RPC_X_PIPE_EMPTY ((NTSTATUS)279)
#define RPC_X_SS_CANNOT_GET_CALL_HANDLE ((NTSTATUS)266)
#define RPC_X_SS_CHAR_TRANS_OPEN_FAIL ((NTSTATUS)260)
#define RPC_X_SS_CHAR_TRANS_SHORT_FILE ((NTSTATUS)261)
#define RPC_X_SS_CONTEXT_DAMAGED ((NTSTATUS)264)
#define RPC_X_SS_HANDLES_MISMATCH ((NTSTATUS)265)
#define RPC_X_WRONG_ES_VERSION ((NTSTATUS)272)
#define RPC_X_WRONG_PIPE_VERSION ((NTSTATUS)277)
#define RPC_X_WRONG_STUB_VERSION ((NTSTATUS)273)


typedef struct _RTLP_RUN { NTSTATUS Status; USHORT Error; } RTLP_RUN;
extern const RTLP_RUN RtlpRunTable[];


/* ---------------------------------------------------------------------------
 * Runtime data structures, layouts copied from the tree's winnt.h.
 * These unblock the RTL families that were failing on missing types.
 * ------------------------------------------------------------------------- */

/* RTL_CRITICAL_SECTION (winnt.h) - used by critsect/fastlock families */
typedef struct _RTL_CRITICAL_SECTION_DEBUG {
    unsigned short Type, CreatorBackTraceIndex;
    struct _RTL_CRITICAL_SECTION *CriticalSection;
    LIST_ENTRY ProcessLocksList;
    unsigned long EntryCount, ContentionCount, Flags, CreatorBackTraceIndexHigh, SpareUSHORT;
} RTL_CRITICAL_SECTION_DEBUG, *PRTL_CRITICAL_SECTION_DEBUG;

typedef struct _RTL_CRITICAL_SECTION {
    PRTL_CRITICAL_SECTION_DEBUG DebugInfo;
    long LockCount, RecursionCount;
    void *OwningThread;
    void *LockSemaphore;
    unsigned long SpinCount;
} RTL_CRITICAL_SECTION, *PRTL_CRITICAL_SECTION;

/* SLIST with the real 16-byte header the RTL expects */
typedef struct _XP_SLIST_HEADER { unsigned long long Alignment; unsigned long Depth, Sequence; } XP_SLIST_HEADER;
typedef XP_SLIST_HEADER *PSLIST_HEADER_T;

/* security descriptors: opaque here; the RTL only manipulates pointers */
typedef void *PSECURITY_DESCRIPTOR, *PACL, *PSID;

/* Minimal PEB: only the fields the RTL touches, in the order the tree's
   public/sdk/inc/pebteb.h declares them. Fields before FastPebLock are not
   used by the RTL, so they are collapsed into padding of the right size. */
typedef struct _XP_PEB {
    unsigned char Reserved1[0x18];           /* up to ImageBaseAddress */
    void *ImageBaseAddress;
    void *Ldr;
    struct _XP_RTL_USER_PROCESS_PARAMETERS *ProcessParameters;
    void *SubSystemData;
    void *ProcessHeap;
    void *FastPebLock;                       /* PRTL_CRITICAL_SECTION */
    void *FastPebLockRoutine;
    void *FastPebUnlockRoutine;
    unsigned long EnvironmentUpdateCount;
} XP_PEB, *PPEB;
typedef void *PLDR_DATA_TABLE_ENTRY;

/* RTL_USER_PROCESS_PARAMETERS: only Environment, which environ.c uses.
   In the real PEB this struct's Environment field is at offset 0x48. */
typedef struct _XP_CURDIR { UNICODE_STRING DosPath; void *Handle; } XP_CURDIR;
typedef struct _XP_RTL_USER_PROCESS_PARAMETERS {
    unsigned char Reserved1[0x20];
    XP_CURDIR CurrentDirectory;
    UNICODE_STRING DllPath;
    UNICODE_STRING ImagePathName;
    UNICODE_STRING CommandLine;
    void *Environment;
    unsigned long EnvironmentSize;
    unsigned long EnvironmentUpdateCount;
} XP_RTL_USER_PROCESS_PARAMETERS;


extern XP_PEB XpPeb;
static __inline__ PPEB NtCurrentPeb_(void) { return &XpPeb; }
#define NtCurrentPeb NtCurrentPeb_

#define PAGE_SIZE 0x1000
#define PAGE_SHIFT 12
#define MEMORY_ALLOCATION_ALIGNMENT 16
#define MAXIMUM_WAIT_OBJECTS 64
#define MAXIMUM_SUSPEND_COUNT 0xFF


/* RTL_HANDLE_TABLE, verbatim from the tree's public/sdk/inc/nturtl.h */
typedef struct _RTL_HANDLE_TABLE_ENTRY {
    union {
        unsigned long Flags;
        struct _RTL_HANDLE_TABLE_ENTRY *NextFree;
    };
} RTL_HANDLE_TABLE_ENTRY, *PRTL_HANDLE_TABLE_ENTRY;

#define RTL_HANDLE_ALLOCATED (unsigned short)0x0001

typedef struct _RTL_HANDLE_TABLE {
    unsigned long MaximumNumberOfHandles;
    unsigned long SizeOfHandleTableEntry;
    unsigned long Reserved[2];
    PRTL_HANDLE_TABLE_ENTRY FreeHandles, CommittedHandles, UnCommittedHandles, MaxReservedHandles;
} RTL_HANDLE_TABLE, *PRTL_HANDLE_TABLE;

typedef struct _MEMORY_BASIC_INFORMATION {
    void *BaseAddress, *AllocationBase;
    unsigned long AllocationProtect, RegionSize, State, Protect, Type;
} MEMORY_BASIC_INFORMATION, *PMEMORY_BASIC_INFORMATION;

/* memory allocation flags, from the tree winnt.h */
#define MEM_4MB_PAGES 0x80000000
#define MEM_COMMIT 0x1000
#define MEM_DECOMMIT 0x4000
#define MEM_FREE 0x10000
#define MEM_MAPPED 0x40000
#define MEM_PHYSICAL 0x400000
#define MEM_PRIVATE 0x20000
#define MEM_RELEASE 0x8000
#define MEM_RESERVE 0x2000
#define MEM_RESET 0x80000
#define MEM_TOP_DOWN 0x100000
#define MEM_WRITE_WATCH 0x200000

/* page protection / section flags, from the tree winnt.h */
#define PAGE_EXECUTE 0x10
#define PAGE_EXECUTE_READ 0x20
#define PAGE_EXECUTE_READWRITE 0x40
#define PAGE_EXECUTE_WRITECOPY 0x80
#define PAGE_GUARD 0x100
#define PAGE_NOACCESS 0x01
#define PAGE_NOCACHE 0x200
#define PAGE_READONLY 0x02
#define PAGE_READWRITE 0x04
#define PAGE_WRITECOMBINE 0x400
#define PAGE_WRITECOPY 0x08
#define SEC_COMMIT 0x8000000
#define SEC_FILE 0x800000
#define SEC_IMAGE 0x1000000
#define SEC_NOCACHE 0x10000000
#define SEC_RESERVE 0x4000000


/* Virtual memory back end for RTL handle tables. In a real kernel these are
   Nt* syscalls; freestanding, a simple bump allocator over a static arena is
   enough and keeps the XP logic (which is what we verify) untouched. */
extern unsigned char XpHeapArena[];
extern unsigned long XpHeapUsed;
extern unsigned long XpHeapSize;

static __inline__ long XpNtAllocateVirtualMemory(void *Process, void **Base, unsigned long ZeroBits,
        unsigned long *RegionSize, unsigned long AllocType, unsigned long Protect) {
    (void)Process;
    unsigned long sz = (*RegionSize + 0xFFF) & ~0xFFFUL;
    unsigned char *base;
    (void)ZeroBits; (void)AllocType; (void)Protect;
    if (*Base == 0) {
        if (XpHeapUsed + sz > XpHeapSize) return (long)0xC0000017L;  /* NO_MEMORY */
        base = XpHeapArena + XpHeapUsed;
        XpHeapUsed += sz;
        *Base = base;
    } else {
        base = (unsigned char *)*Base;
    }
    { unsigned long i; for (i = 0; i < sz; i++) base[i] = 0; }
    *RegionSize = sz;
    return 0;
}

static __inline__ long XpNtFreeVirtualMemory(void *Process, void **Base, unsigned long *RegionSize, unsigned long FreeType) {
    (void)Process; (void)Base; (void)RegionSize; (void)FreeType;
    return 0;   /* arena is never returned to the OS */
}

#define NtAllocateVirtualMemory XpNtAllocateVirtualMemory
#define NtFreeVirtualMemory     XpNtFreeVirtualMemory
#define NtCurrentProcess()      ((void *)-1)


/* GENERATE_NAME_CONTEXT, verbatim field list from the tree's ntrtl.h */
typedef struct _GENERATE_NAME_CONTEXT {
    USHORT Checksum;
    BOOLEAN ChecksumInserted;
    UCHAR NameLength;
    WCHAR NameBuffer[8];
    ULONG ExtensionLength;
    WCHAR ExtensionBuffer[4];
    ULONG LastIndexValue;
} GENERATE_NAME_CONTEXT, *PGENERATE_NAME_CONTEXT;

/* SLIST_HEADER as the real union from the tree's winnt.h */
typedef struct _XP_SLIST_ENTRY { struct _XP_SLIST_ENTRY *Next; } XP_SLIST_ENTRY, *PXP_SLIST_ENTRY;
typedef union _SLIST_HEADER_REAL {
    unsigned long long Alignment;
    struct { XP_SLIST_ENTRY Next; unsigned short Depth; unsigned short Sequence; };
} SLIST_HEADER_REAL;
typedef SLIST_HEADER_REAL *PSLIST_HEADER;
typedef SLIST_HEADER_REAL SLIST_HEADER;

/* --- RTL_USER_PROCESS_PARAMETERS, verbatim field list from the tree's
     ntrtl.h (line 3717) — needed by environ.c via PEB->ProcessParameters -- */
typedef struct _RTL_USER_PROCESS_PARAMETERS {
    ULONG MaximumLength;
    ULONG Length;
    ULONG Flags;
    ULONG DebugFlags;
    void *ConsoleHandle;
    ULONG ConsoleFlags;
    void *StandardInput;
    void *StandardOutput;
    void *StandardError;
    UNICODE_STRING CurrentDirectory_DosPath;
    void *CurrentDirectory_Handle;
    UNICODE_STRING DllPath;
    UNICODE_STRING ImagePathName;
    UNICODE_STRING CommandLine;
    void *Environment;              /* NtAllocateVirtualMemory */
} RTL_USER_PROCESS_PARAMETERS, *PRTL_USER_PROCESS_PARAMETERS;

/* --- CONTEXT: i386 layout copied from the tree's winnt.h (line ~1996) ---
   Only the fields RtlAssert2 needs (ContextFlags) are meaningful here; the
   rest is the real x86 CONTEXT shape so the struct can never be mis-sized. */
typedef struct _FLOATING_SAVE_AREA {
    DWORD ControlWord, StatusWord, TagWord, ErrorOffset, ErrorSelector;
    DWORD DataOffset, DataSelector, Cr0NpxState;
    BYTE RegisterArea[80];
} FLOATING_SAVE_AREA;

typedef struct _CONTEXT {
    DWORD ContextFlags;
    DWORD Dr0, Dr1, Dr2, Dr3, Dr6, Dr7;
    FLOATING_SAVE_AREA FloatSave;
    DWORD SegGs, SegFs, SegEs, SegDs;
    DWORD Edi, Esi, Ebx, Edx, Ecx, Eax;
    DWORD Ebp, Eip, SegCs, EFlags, Esp, SegSs;
    BYTE ExtendedRegisters[512];
} CONTEXT;

static __inline__ void RtlCaptureContext(CONTEXT *ContextRecord) {
    __asm__ volatile(
        "movl %%esp, %0\n\t"
        "movl %%ebp, %1\n\t"
        "pushfl\n\t"
        "popl %2"
        : "=r"(ContextRecord->Esp), "=r"(ContextRecord->Ebp), "=r"(ContextRecord->EFlags)
    );
    ContextRecord->ContextFlags = 0x00010001;   /* CONTEXT_i386 | CONTEXT_CONTROL */
}

/* --- DbgPrompt: reads from the QEMU debug console. In freestanding tests
   there is no debugger; return 0 so the caller's prompt loop sees EOF and
   treats it as "no response". assert.c's `Response` buffer is then left
   zeroed, and its while loop falls through to 'terminate Process'. */
static __inline__ unsigned long DbgPrompt(const char *Prompt, char *Response, unsigned long Length) {
    unsigned long i;
    (void)Prompt;
    if (Length) { for (i = 0; i < Length; i++) Response[i] = 0; }
    return 0;
}

static __inline__ void DbgBreakPoint(void) {
    __asm__ volatile("int $3");
}

#define NtCurrentThread() ((void *)-2)
static __inline__ long ZwTerminateProcess(void *Process, long Status) {
    (void)Process; (void)Status;
    for(;;) __asm__ volatile("hlt");
    return 0; /* not reached */
}
static __inline__ long ZwTerminateThread(void *Thread, long Status) {
    (void)Thread; (void)Status;
    for(;;) __asm__ volatile("hlt");
    return 0; /* not reached */
}

/* --- ZwQueryVirtualMemory: freestanding stand-in for the NT syscall.
   Only MemoryBasicInformation is needed by environ.c; report the current
   allocated heap size as the "region size" so the caller can query how much
   room is left, and confirm the region starts inside the arena. */
#define MemoryBasicInformation 0
static __inline__ long XpZwQueryVirtualMemory(void *Process, void *BaseAddress,
        unsigned long InfoClass, void *Buffer, unsigned long Length, unsigned long *ReturnLength) {
    (void)Process; (void)InfoClass; (void)Length;
    if ((unsigned char *)BaseAddress < XpHeapArena ||
        (unsigned char *)BaseAddress >= XpHeapArena + XpHeapSize) {
        return (long)0xC00000EFL;   /* STATUS_NOT_COMMITTED */
    }
    if (Buffer) {
        MEMORY_BASIC_INFORMATION *mbi = (MEMORY_BASIC_INFORMATION *)Buffer;
        mbi->BaseAddress       = BaseAddress;
        mbi->AllocationBase    = XpHeapArena;
        mbi->AllocationProtect = PAGE_READWRITE;
        mbi->RegionSize        = XpHeapSize - ((unsigned char *)BaseAddress - XpHeapArena);
        mbi->State             = MEM_COMMIT;
        mbi->Protect           = PAGE_READWRITE;
        mbi->Type              = MEM_PRIVATE;
    }
    if (ReturnLength) *ReturnLength = sizeof(MEMORY_BASIC_INFORMATION);
    return 0;
}
#define ZwQueryVirtualMemory XpZwQueryVirtualMemory
#define ZwAllocateVirtualMemory XpNtAllocateVirtualMemory
#define ZwFreeVirtualMemory     XpNtFreeVirtualMemory

/* --- PEB / process parameters wiring for the test environment -----------
   environ.c reaches the environment via NtCurrentPeb()->ProcessParameters->
   Environment and guards access with FastPebLock. Both live in a small static
   block that tests initialise through XpEnvInitProcessParameters(). */
extern RTL_USER_PROCESS_PARAMETERS XpProcessParameters;
extern RTL_CRITICAL_SECTION XpPebLock;
static __inline__ void XpEnvInitProcessParameters(void) {
    XpProcessParameters.MaximumLength = sizeof(XpProcessParameters);
    XpProcessParameters.Length = sizeof(XpProcessParameters);
    XpProcessParameters.Environment = 0;
    XpPeb.ProcessParameters = &XpProcessParameters;
    XpPeb.FastPebLock = &XpPebLock;
}

#define STATUS_BAD_COMPRESSION_BUFFER ((NTSTATUS)0xC0000242)
/* verbatim from the tree's ntdef.h */
#define RTL_CONSTANT_STRING(s) { sizeof( s ) - sizeof( (s)[0] ), sizeof( s ), s }

/* prototypes environ.c calls; bodies live in other RTL files */
NTSTATUS RtlFindCharInUnicodeString(unsigned long Flags, const UNICODE_STRING *S,
                                    const UNICODE_STRING *CharSet, unsigned short *PrefixLen);
#define RTL_FIND_CHAR_IN_UNICODE_STRING_START_AT_END        0x00000001
#define RTL_FIND_CHAR_IN_UNICODE_STRING_COMPLEMENT_CHAR_SET 0x00000002
#define RTL_FIND_CHAR_IN_UNICODE_STRING_CASE_INSENSITIVE    0x00000004
BOOLEAN RtlDosPathNameToNtPathName_U(const unsigned short *DosName,
                                     UNICODE_STRING *NtName, unsigned short **FilePart,
                                     void *RelativeName);

static const UNICODE_STRING RtlDosPathSeperatorsString = { 4, 4, L"\\/" };
static const UNICODE_STRING RtlAlternateDosPathSeperatorString = { 2, 2, L"/" };

/* SID structures, verbatim from the tree's public/sdk/inc/winnt.h */
#ifndef SID_IDENTIFIER_AUTHORITY_DEFINED
#define SID_IDENTIFIER_AUTHORITY_DEFINED
typedef struct _SID_IDENTIFIER_AUTHORITY { unsigned char Value[6]; }
    SID_IDENTIFIER_AUTHORITY, *PSID_IDENTIFIER_AUTHORITY;
#endif
#ifndef SID_DEFINED
#define SID_DEFINED
typedef struct _SID {
    unsigned char Revision;
    unsigned char SubAuthorityCount;
    SID_IDENTIFIER_AUTHORITY IdentifierAuthority;
    unsigned long SubAuthority[1];
} SID, *PISID;
#endif
typedef void *PSID;
typedef unsigned long ACCESS_MASK, *PACCESS_MASK;

#define SID_REVISION 1
#define SID_MAX_SUB_AUTHORITIES 15
#define SECURITY_MAX_SID_SIZE 68
#define ANYSIZE_ARRAY 1
#define SECURITY_NULL_SID_AUTHORITY        {0,0,0,0,0,0}
#define SECURITY_WORLD_SID_AUTHORITY       {0,0,0,0,0,1}
#define SECURITY_LOCAL_SID_AUTHORITY       {0,0,0,0,0,2}
#define SECURITY_CREATOR_SID_AUTHORITY     {0,0,0,0,0,3}
#define SECURITY_NT_AUTHORITY              {0,0,0,0,0,5}


/* SID helpers declared in the tree's ntrtl.h; definitions are in sertl.c.
   Needed as prototypes so sertl's own bodies can call each other. */
unsigned char *RtlSubAuthorityCountSid(PSID Sid);
unsigned long *RtlSubAuthoritySid(PSID Sid, unsigned long SubAuthority);
PSID_IDENTIFIER_AUTHORITY RtlIdentifierAuthoritySid(PSID Sid);
unsigned long RtlLengthSid(PSID Sid);
unsigned long RtlLengthRequiredSid(unsigned long SubAuthorityCount);
NTSTATUS RtlCopySid(unsigned long DestinationSidLength, PSID DestinationSid, PSID SourceSid);
NTSTATUS RtlInitializeSid(PSID Sid, PSID_IDENTIFIER_AUTHORITY IdentifierAuthority,
                          unsigned char SubAuthorityCount);
BOOLEAN RtlValidSid(PSID Sid);
BOOLEAN RtlEqualSid(PSID Sid1, PSID Sid2);
BOOLEAN RtlEqualPrefixSid(PSID Sid1, PSID Sid2);
BOOLEAN RtlAreAllAccessesGranted(ACCESS_MASK GrantedAccess, ACCESS_MASK DesiredAccess);
BOOLEAN RtlAreAnyAccessesGranted(ACCESS_MASK GrantedAccess, ACCESS_MASK DesiredAccess);


/* helpers sertl.c uses; both are inline/macro in the tree, so reproduce them
   here rather than linking them.
     SeLengthSid        -> base/ntos/inc/se.h:559 (macro)
     RtlpSubAuthoritySid-> public/internal/base/inc/sertlp.h:300 (FORCEINLINE)
     RtlEqualMemory     -> standard NT memory compare
*/
#define SeLengthSid(Sid) (8 + (4 * ((SID *)(Sid))->SubAuthorityCount))
static __inline__ unsigned long *RtlpSubAuthoritySid(PSID Sid, unsigned long SubAuthority) {
    PISID s = (PISID)Sid;
    return (unsigned long *)((unsigned char *)s->SubAuthority + (sizeof(unsigned long) * SubAuthority));
}
static __inline__ BOOLEAN RtlEqualMemory(const void *a, const void *b, unsigned long n) {
    const unsigned char *x = (const unsigned char *)a, *y = (const unsigned char *)b;
    while (n--) if (*x++ != *y++) return 0;
    return 1;
}


/* RTL_HASH flags + prototypes, verbatim from the tree's ntrtl.h */
#define HASH_STRING_ALGORITHM_DEFAULT (0)
#define HASH_STRING_ALGORITHM_X65599  (1)
#define HASH_STRING_ALGORITHM_INVALID (0xffffffffL)
NTSTATUS RtlHashUnicodeString(const UNICODE_STRING *String, BOOLEAN CaseInSensitive,
                              ULONG HashAlgorithm, ULONG *HashValue);
LONG RtlCompareUnicodeString(const UNICODE_STRING *a, const UNICODE_STRING *b,
                             BOOLEAN CaseInSensitive);
BOOLEAN RtlEqualUnicodeString(const UNICODE_STRING *a, const UNICODE_STRING *b,
                              BOOLEAN CaseInSensitive);
long RtlCompareUnicodeStringRaw(const UNICODE_STRING *a, const UNICODE_STRING *b, BOOLEAN ci);
VOID RtlCopyUnicodeString(UNICODE_STRING *Dest, const UNICODE_STRING *Src);
NTSTATUS RtlAppendUnicodeToString(UNICODE_STRING *Dest, const WCHAR *Src);
NTSTATUS RtlAppendUnicodeStringToString(UNICODE_STRING *Dest, const UNICODE_STRING *Src);
WCHAR RtlUpcaseUnicodeChar(WCHAR c);
WCHAR RtlDowncaseUnicodeChar(WCHAR c);


/* NLS_* case macros, transcribed from base/ntos/rtl/ntrtlp.h:239-265.
   The non-ASCII branch needs the NLS 844 tables which a freestanding build
   does not carry; those code points pass through unchanged. ASCII behaviour
   is identical to XP. */
#define NLS_UPCASE(wch) \
    (((wch) < 'a') ? (wch) : (((wch) <= 'z') ? (wch) - ('a'-'A') : (wch)))
#define NLS_DOWNCASE(wch) \
    (((wch) < 'A') ? (wch) : (((wch) <= 'Z') ? (wch) + ('a'-'A') : (wch)))


/* On x86 the razzle build gets RtlInitUnicodeString from an inline in the pch
   chain (string.c only defines the out-of-line version for non-x86). Provide
   the same semantics here. */
static __inline__ void RtlInitUnicodeString(UNICODE_STRING *Dest, const WCHAR *Src) {
    if (Src) {
        unsigned long n = 0; while (Src[n]) n++;
        Dest->Buffer = (WCHAR *)Src;
        Dest->Length = (unsigned short)(n * 2);
        Dest->MaximumLength = (unsigned short)(n * 2 + 2);
    } else {
        Dest->Buffer = 0; Dest->Length = 0; Dest->MaximumLength = 0;
    }
}

#endif /* _XPKERNEL_H_ */
