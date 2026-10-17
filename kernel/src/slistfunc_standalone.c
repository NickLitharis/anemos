
/* kernel-runtime macros the XP sources assume */
#ifndef RTL_PAGED_CODE
#define RTL_PAGED_CODE()
#endif
#ifndef InterlockedCompareExchange
static __inline__ long InterlockedCompareExchange(volatile long *d, long x, long c){
  long old; __asm__ __volatile__("lock cmpxchgl %2, %1" : "=a"(old), "+m"(*d) : "r"(x), "a"(c) : "memory"); return old; }
#endif
typedef unsigned char UCHAR; typedef unsigned short USHORT, *PUSHORT;
typedef unsigned long ULONG, *PULONG; typedef long LONG, *PLONG; typedef long NTSTATUS;
typedef unsigned long long ULONGLONG, *PULONGLONG; typedef unsigned int ULONG32;
typedef void *PVOID, *PULONG_PTR; typedef int BOOLEAN;
#define FASTCALL __attribute__((fastcall))
#define NTKERNELAPI
#define NTAPI
#define IN
#define OUT
#define OPTIONAL
#define CONST const
#define VOID void
#ifndef TRUE
#define TRUE 1
#define FALSE 0
#endif
typedef struct _SINGLE_LIST_ENTRY { struct _SINGLE_LIST_ENTRY *Next; } SINGLE_LIST_ENTRY, *PSINGLE_LIST_ENTRY;
typedef struct _SLIST_HEADER { ULONGLONG Alignment; ULONG Depth; ULONG Sequence; } SLIST_HEADER, *PSLIST_HEADER;
#define InitializeSListHead(h) do { (h)->Alignment = 0; (h)->Depth = 0; (h)->Sequence = 0; } while (0)
#define QueryDepthSList(h) ((USHORT)((h)->Depth))
/*++

Copyright (c) 2001  Microsoft Corporation

Module Name:

    slistfunc.c

Abstract:

    This module implements WIN64 SLIST functions.

Author:

    David N. Cutler (davec) 11-Feb-2001

Revision History:

--*/



VOID
RtlpInitializeSListHead (
    IN PSLIST_HEADER SListHead
    )

/*++

Routine Description:

    This function initializes a sequenced singly linked listhead.

Arguments:

    SListHead - Supplies a pointer to a sequenced singly linked listhead.

Return Value:

    None.

--*/

{

    InitializeSListHead(SListHead);
    return;
}

USHORT
RtlpQueryDepthSList (
    IN PSLIST_HEADER SListHead
    )

/*++

Routine Description:

    This function queries the depth of the specified SLIST.

Arguments:

    SListHead - Supplies a pointer to a sequenced singly linked listhead.

Return Value:

    The current depth of the specified SLIST is returned as the function
    value.

--*/

{
     return QueryDepthSList(SListHead);
}
