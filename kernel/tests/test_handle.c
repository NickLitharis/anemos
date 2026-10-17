/* v1.1: XP handle.c — RTL handle tables (used by atoms and local/global mem). */
typedef unsigned long ULONG; typedef unsigned short USHORT; typedef unsigned char BOOLEAN;

typedef struct _E { union { ULONG Flags; struct _E *NextFree; }; } E, *PE;
typedef struct {
    ULONG MaximumNumberOfHandles, SizeOfHandleTableEntry, Reserved[2];
    PE FreeHandles, CommittedHandles, UnCommittedHandles, MaxReservedHandles;
} HT;

void RtlInitializeHandleTable(ULONG MaximumNumberOfHandles, ULONG SizeOfHandleTableEntry, HT *HandleTable);
PE RtlAllocateHandle(HT *HandleTable, ULONG *HandleIndex);
BOOLEAN RtlFreeHandle(HT *HandleTable, PE Handle);
BOOLEAN RtlIsValidHandle(HT *HandleTable, PE Handle);
BOOLEAN RtlIsValidIndexHandle(HT *HandleTable, ULONG HandleIndex, PE *Handle);

static void outb(unsigned short p, unsigned char v){ __asm__ volatile("outb %0,%1" :: "a"(v), "Nd"(p)); }
static void puts_(const char*s){while(*s)outb(0xE9,(unsigned char)*s++);}
static void puthex(unsigned long v){const char*d="0123456789ABCDEF";int i,st=0;
 for(i=28;i>=0;i-=4){unsigned long n=(v>>i)&0xF; if(n||st||i==0){st=1;outb(0xE9,(unsigned char)d[n]);}}}
static int check(const char*n,unsigned long g,unsigned long w){puts_(n);puts_("=");puthex(g);
 int ok=(g==w);puts_(ok?" OK\n":" FAIL\n");return ok;}

static unsigned char backing[4096];

void _start(void) {
    int ok = 1;
    HT t;
    ULONG idx = 0xFFFFFFFF;
    PE E1 = 0, E2 = 0;

    RtlInitializeHandleTable(8, sizeof(E), &t);
    ok &= check("maxHandles", t.MaximumNumberOfHandles, 8);
    ok &= check("entrySize", t.SizeOfHandleTableEntry, sizeof(E));
    ok &= check("freeList.initial", (unsigned long)(t.FreeHandles != 0), 0);

    E1 = RtlAllocateHandle(&t, &idx);
    ok &= check("alloc1.nonnull", (unsigned long)(E1 != 0), 1);
    ok &= check("alloc1.index", idx, 0);
    /* XP leaves the allocated bit for the caller to set (see handle.c comment) */
    ok &= check("alloc1.flagcleared", E1->Flags, 0);
    E1->Flags |= 1;                      /* RTL_HANDLE_ALLOCATED */
    ok &= check("alloc1.valid", RtlIsValidHandle(&t, E1), 1);

    E2 = RtlAllocateHandle(&t, &idx);
    ok &= check("alloc2.index", idx, 1);
    E2->Flags |= 1;
    ok &= check("alloc2.valid", RtlIsValidHandle(&t, E2), 1);

    ok &= check("free1", RtlFreeHandle(&t, E1), 1);
    ok &= check("free1.valid", RtlIsValidHandle(&t, E1), 0);
    ok &= check("free2.valid", RtlIsValidHandle(&t, E2), 1);

    { PE got = 0; ok &= check("validIndex0", RtlIsValidIndexHandle(&t, 0, &got), 0); }

    puts_(ok ? "XP-MODERNIZATION-V1.1: XP handle.c EXECUTED AND VERIFIED\n"
             : "XP-MODERNIZATION-V1.1: MISMATCH\n");
    (void)backing;
    for(;;) __asm__ volatile("hlt");
}
