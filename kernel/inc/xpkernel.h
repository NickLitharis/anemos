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
#define NT_SUCCESS(s)                   (((NTSTATUS)(s)) >= 0)

#define UNICODE_NULL        ((WCHAR)0)
#define UNICODE_STRING_MAX_CHARS 32767


/* --- generic-function types used by the RTL tables --------------------- */
typedef unsigned char CCHAR_T;
typedef BOOLEAN (*PRTL_GENERIC_COMPARE_ROUTINE)(void *Table, PVOID First, PVOID Second);
typedef PVOID   (*PRTL_GENERIC_ALLOCATE_ROUTINE)(void *Table, ULONG ByteSize);
typedef void    (*PRTL_GENERIC_FREE_ROUTINE)(void *Table, PVOID Buffer);
typedef void    *RTL_GENERIC_TABLE_PLACEHOLDER;

typedef struct _RTL_SPLAY_LINKS { struct _RTL_SPLAY_LINKS *Parent, *LeftChild, *RightChild; } RTL_SPLAY_LINKS;
typedef struct { struct _RTL_SPLAY_LINKS *Root; } RTL_SPLAY_HEADER_PLACEHOLDER;

typedef struct _RTL_BITMAP { ULONG SizeOfBitMap; PULONG Buffer; } RTL_BITMAP, *PRTL_BITMAP;

typedef struct _PREFIX_TABLE_ENTRY {
    struct _PREFIX_TABLE_ENTRY *NextPrefixTree;
    struct { unsigned short Length, MaximumLength; unsigned short *Buffer; } Prefix;
} PREFIX_TABLE_ENTRY, *PPREFIX_TABLE_ENTRY;
typedef struct _PREFIX_TABLE { PREFIX_TABLE_ENTRY *NextPrefixTree; } PREFIX_TABLE, *PPREFIX_TABLE;

#define try      /* SEH is unavailable freestanding */
#define except(a) if (0)
#define finally  if (1)
#define leave    break


typedef enum _RTL_GENERIC_COMPARE_RESULTS {
    GenericLessThan, GenericGreaterThan, GenericEqual
} RTL_GENERIC_COMPARE_RESULTS;
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
    unsigned long i; (void)alloc;
    if (!d->Buffer) return STATUS_INVALID_PARAMETER;
    for (i = 0; i < s->Length && i * 2 < d->MaximumLength; i++) d->Buffer[i] = (WCHAR)(unsigned char)s->Buffer[i];
    d->Length = (unsigned short)(i * 2);
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
#define RtlMoveMemory RtlMoveMemory_
#define RtlZeroMemory RtlZeroMemory_
#define RtlFillMemory RtlFillMemory_

#endif /* _XPKERNEL_H_ */
