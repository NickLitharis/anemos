/* v0.8: XP avltable.c — a balanced binary tree with rotation logic. */
typedef unsigned long ULONG; typedef long NTSTATUS; typedef unsigned char BOOLEAN;
typedef void *PVOID;
typedef struct _RTL_BALANCED_LINKS { struct _RTL_BALANCED_LINKS *P, *L, *R; char Balance; unsigned char Reserved[3]; } RTL_BALANCED_LINKS;
typedef long (*PCMP)(struct _RTL_AVL_TABLE *, void *, void *);
typedef void *(*PALLOC)(struct _RTL_AVL_TABLE *, unsigned long);
typedef void (*PFREE)(struct _RTL_AVL_TABLE *, void *);
typedef struct _RTL_AVL_TABLE {
    RTL_BALANCED_LINKS BalancedRoot; PVOID OrderedPointer;
    ULONG WhichOrderedElement, NumberGenericTableElements, DepthOfTree;
    PVOID RestartKey; ULONG DeleteCount;
    PCMP CompareRoutine; PALLOC AllocateRoutine; PFREE FreeRoutine; PVOID TableContext;
} RTL_AVL_TABLE;

void RtlInitializeGenericTableAvl(RTL_AVL_TABLE *, PCMP, PALLOC, PFREE, PVOID);
PVOID RtlInsertElementGenericTableAvl(RTL_AVL_TABLE *, PVOID, ULONG, PBOOLEAN);
PVOID RtlLookupElementGenericTableAvl(RTL_AVL_TABLE *, PVOID);
ULONG RtlNumberGenericTableElementsAvl(RTL_AVL_TABLE *);

static void outb(unsigned short p, unsigned char v){ __asm__ volatile("outb %0,%1" :: "a"(v), "Nd"(p)); }
static void puts_(const char*s){while(*s)outb(0xE9,(unsigned char)*s++);}
static void puthex(unsigned long v){const char*d="0123456789ABCDEF";int i,st=0;
 for(i=28;i>=0;i-=4){unsigned long n=(v>>i)&0xF; if(n||st||i==0){st=1;outb(0xE9,(unsigned char)d[n]);}}}
static int check(const char*n,unsigned long g,unsigned long w){puts_(n);puts_("=");puthex(g);
 int ok=(g==w);puts_(ok?" OK\n":" FAIL\n");return ok;}

/* element: RTL_BALANCED_LINKS must be first, then a key */
typedef struct { unsigned long Key; } ELEM;

static int used = 0;

static long cmp(struct _RTL_AVL_TABLE *t, void *a, void *b) {
    /* a and b point at the stored node; user data follows the links header.
       For the lookup buffer XP passes the caller's buffer directly. */
    unsigned long ka = ((ELEM *)a)->Key;
    unsigned long kb = ((ELEM *)b)->Key;
    (void)t;
    if (ka < kb) return 0;      /* GenericLessThan */
    if (ka > kb) return 1;      /* GenericGreaterThan */
    return 2;                   /* GenericEqual */
}
static unsigned char poolraw[32][64];
static void *alloc_(struct _RTL_AVL_TABLE *t, unsigned long n) { (void)t; (void)n; return poolraw[used++]; }

static void free_(struct _RTL_AVL_TABLE *t, void *p) { (void)t; (void)p; }

void _start(void) {
    int ok = 1, i;
    RTL_AVL_TABLE t;
    RtlInitializeGenericTableAvl(&t, cmp, alloc_, free_, 0);
    ok &= check("emptyCount", RtlNumberGenericTableElementsAvl(&t), 0);

    for (i = 1; i <= 7; i++) {
        ELEM tmp; BOOLEAN nw = 0;
        tmp.Key = (unsigned long)i * 10;
        RtlInsertElementGenericTableAvl(&t, &tmp, sizeof(tmp), &nw);
        ok &= check("insert.new", nw, 1);
    }
    ok &= check("count", RtlNumberGenericTableElementsAvl(&t), 7);

    { ELEM k; void *r; k.Key = 40; r = RtlLookupElementGenericTableAvl(&t, &k);
      ok &= check("lookup40.found", (unsigned long)(r != 0), 1);
      ok &= check("lookup45.absent", (unsigned long)(RtlLookupElementGenericTableAvl(&t, (ELEM[]){ {45} }) != 0), 0); }

    ok &= check("depth", t.DepthOfTree, 3);   /* balanced: 7 nodes -> depth 3 */

    puts_(ok ? "XP-MODERNIZATION-V0.8: XP avltable.c EXECUTED AND VERIFIED\n"
             : "XP-MODERNIZATION-V0.8: MISMATCH\n");
    for(;;) __asm__ volatile("hlt");
}
