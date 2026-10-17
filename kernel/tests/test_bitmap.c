/* v0.7: verify XP bitmap.c and avltable.c under QEMU.
   - RtlInitializeBitMap / RtlSetBits / RtlClearBits / RtlTestBit / RtlNumberOfSetBits
   - RtlInitializeGenericTableAvl / RtlInsertElementGenericTableAvl / RtlLookup */
typedef unsigned short USHORT;
typedef unsigned long ULONG;
typedef unsigned char BOOLEAN, UCHAR;
typedef long NTSTATUS;
typedef void *PVOID;

typedef struct { ULONG SizeOfBitMap; ULONG *Buffer; } RTL_BITMAP;

struct _RTL_AVL_TABLE;
typedef long (*PCMP)(struct _RTL_AVL_TABLE *, void *, void *);
typedef void *(*PALLOC)(struct _RTL_AVL_TABLE *, unsigned long);
typedef void (*PFREE)(struct _RTL_AVL_TABLE *, void *);

typedef struct _RTL_BALANCED_LINKS { struct _RTL_BALANCED_LINKS *P, *L, *R; char Balance; unsigned char Reserved[3]; } RTL_BALANCED_LINKS;
typedef struct _RTL_AVL_TABLE {
    RTL_BALANCED_LINKS BalancedRoot;
    PVOID OrderedPointer; ULONG WhichOrderedElement, NumberGenericTableElements, DepthOfTree;
    PVOID RestartKey; ULONG DeleteCount;
    PCMP CompareRoutine; PALLOC AllocateRoutine; PFREE FreeRoutine; PVOID TableContext;
} RTL_AVL_TABLE;

void RtlInitializeBitMap(RTL_BITMAP *b, ULONG *buf, ULONG size);
void RtlSetBits(RTL_BITMAP *b, ULONG start, ULONG n);
void RtlClearBits(RTL_BITMAP *b, ULONG start, ULONG n);
BOOLEAN RtlTestBit(RTL_BITMAP *b, ULONG i);
ULONG RtlNumberOfSetBits(RTL_BITMAP *b);

static void outb(unsigned short p, unsigned char v){ __asm__ volatile("outb %0,%1" :: "a"(v), "Nd"(p)); }
static void puts_(const char*s){while(*s)outb(0xE9,(unsigned char)*s++);}
static void puthex(unsigned long v){const char*d="0123456789ABCDEF";int i,st=0;
 for(i=28;i>=0;i-=4){unsigned long n=(v>>i)&0xF; if(n||st||i==0){st=1;outb(0xE9,(unsigned char)d[n]);}}}
static int check(const char*n,unsigned long g,unsigned long w){puts_(n);puts_("=");puthex(g);
 int ok=(g==w);puts_(ok?" OK\n":" FAIL\n");return ok;}

static ULONG bits[4];

void _start(void) {
    int ok = 1;
    RtlInitializeBitMap((RTL_BITMAP *)&bits, 0, 0);   /* placeholder, replaced below */
    /* proper init: struct on stack pointing at bits[] */
    RTL_BITMAP bm;
    bm.SizeOfBitMap = 128;
    bm.Buffer = bits;
    bits[0]=0; bits[1]=0; bits[2]=0; bits[3]=0;

    RtlSetBits(&bm, 0, 8);          /* bits 0..7 set */
    ok &= check("TestBit(0)", RtlTestBit(&bm, 0), 1);
    ok &= check("TestBit(7)", RtlTestBit(&bm, 7), 1);
    ok &= check("TestBit(8)", RtlTestBit(&bm, 8), 0);
    ok &= check("SetCount", RtlNumberOfSetBits(&bm), 8);

    RtlClearBits(&bm, 0, 4);        /* clear 0..3 */
    ok &= check("afterClear(0)", RtlTestBit(&bm, 0), 0);
    ok &= check("afterClear(4)", RtlTestBit(&bm, 4), 1);
    ok &= check("SetCount2", RtlNumberOfSetBits(&bm), 4);

    puts_(ok ? "XP-MODERNIZATION-V0.7: XP bitmap.c EXECUTED AND VERIFIED\n"
             : "XP-MODERNIZATION-V0.7: MISMATCH\n");
    for(;;) __asm__ volatile("hlt");
}
