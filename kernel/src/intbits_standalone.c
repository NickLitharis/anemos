
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






#undef RtlInterlockedSetBits

NTKERNELAPI
ULONG
FASTCALL
RtlInterlockedSetBits (
    IN OUT PULONG Flags,
    IN ULONG Flag
    )
/*++

Routine Description:

    This function atomically sets the specified flags in the target

Arguments:

    Flags - Pointer to variable containing current mask.

    Flag  - Flags to set in target

Return Value:

    ULONG - Old value of mask before modification

--*/
{
    ULONG NewFlags, OldFlags;

    OldFlags = *Flags;
    NewFlags = OldFlags | Flag;
    while (NewFlags != OldFlags) {
        NewFlags = InterlockedCompareExchange ((PLONG) Flags, (LONG) NewFlags, (LONG) OldFlags);
        if (NewFlags == OldFlags) {
            break;
        }
        OldFlags = NewFlags;
        NewFlags |= Flag;
    }
    return OldFlags;
}

#undef RtlInterlockedClearBits

NTKERNELAPI
ULONG
FASTCALL
RtlInterlockedClearBits (
    IN OUT PULONG Flags,
    IN ULONG Flag
    )
/*++

Routine Description:

    This function atomically clears the specified flags in the target

Arguments:

    Flags - Pointer to variable containing current mask.

    Flag  - Flags to clear in target

Return Value:

    ULONG - Old value of mask before modification

--*/
{
    ULONG NewFlags, OldFlags;

    OldFlags = *Flags;
    NewFlags = OldFlags & ~Flag;
    while (NewFlags != OldFlags) {
        NewFlags = InterlockedCompareExchange ((PLONG) Flags, (LONG) NewFlags, (LONG) OldFlags);
        if (NewFlags == OldFlags) {
            break;
        }
        OldFlags = NewFlags;
        NewFlags &= ~Flag;
    }
    return OldFlags;
}

#undef RtlInterlockedSetClearBits

NTKERNELAPI
ULONG
FASTCALL
RtlInterlockedSetClearBits (
    IN OUT PULONG Flags,
    IN ULONG sFlag,
    IN ULONG cFlag
    )
/*++

Routine Description:

    This function atomically sets and clears the specified flags in the target

Arguments:

    Flags - Pointer to variable containing current mask.

    sFlag  - Flags to set in target

    CFlag  - Flags to clear in target

Return Value:

    ULONG - Old value of mask before modification

--*/
{
    ULONG NewFlags, OldFlags;

    OldFlags = *Flags;
    NewFlags = (OldFlags | sFlag) & ~cFlag;
    while (NewFlags != OldFlags) {
        NewFlags = InterlockedCompareExchange ((PLONG) Flags, (LONG) NewFlags, (LONG) OldFlags);
        if (NewFlags == OldFlags) {
            break;
        }
        OldFlags = NewFlags;
        NewFlags = (NewFlags | sFlag) & ~cFlag;
    }
    return OldFlags;
}
