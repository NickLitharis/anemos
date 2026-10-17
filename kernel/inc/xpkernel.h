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

#endif /* _XPKERNEL_H_ */
