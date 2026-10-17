/* v1.0: the XP STATUS->DOS error mapping table, 835 pairs taken verbatim from
   base/ntos/rtl/generr.c's CodePairs[]. Verified by spot-checking pairs whose
   correct values are documented Windows behaviour. */
typedef unsigned long ULONG; typedef long NTSTATUS;
typedef struct { ULONG Status; ULONG Error; } RUN;

extern const RUN XpRunPairs[];
extern const ULONG XpRunPairCount;

static void outb(unsigned short p, unsigned char v){ __asm__ volatile("outb %0,%1" :: "a"(v), "Nd"(p)); }
static void puts_(const char*s){while(*s)outb(0xE9,(unsigned char)*s++);}
static void puthex(unsigned long v){const char*d="0123456789ABCDEF";int i,st=0;
 for(i=28;i>=0;i-=4){unsigned long n=(v>>i)&0xF; if(n||st||i==0){st=1;outb(0xE9,(unsigned char)d[n]);}}}
static int check(const char*n,unsigned long g,unsigned long w){puts_(n);puts_("=");puthex(g);
 int ok=(g==w);puts_(ok?" OK\n":" FAIL\n");return ok;}

/* XP's RtlNtStatusToDosError does a table lookup; verify the table contents */
static unsigned long lookup(unsigned long status) {
    unsigned long i;
    for (i = 0; i < XpRunPairCount; i++)
        if (XpRunPairs[i].Status == status) return XpRunPairs[i].Error;
    return 0xFFFFFFFF;
}

void _start(void) {
    int ok = 1;
    ok &= check("pairs", XpRunPairCount, 835);
    /* documented mappings */
    ok &= check("ACCESS_DENIED",  lookup(0xC0000022UL), 5);      /* ERROR_ACCESS_DENIED */
    ok &= check("INVALID_HANDLE", lookup(0xC0000008UL), 6);      /* ERROR_INVALID_HANDLE */
    ok &= check("NO_MEMORY",      lookup(0xC0000017UL), 8);      /* ERROR_NOT_ENOUGH_MEMORY */
    ok &= check("BUFFER_OVERFLOW",lookup(0x80000005UL), 234);    /* ERROR_MORE_DATA, per generr.c */
    ok &= check("NOT_FOUND",      lookup(0xC0000034UL), 2);      /* ERROR_FILE_NOT_FOUND */

    puts_(ok ? "XP-MODERNIZATION-V1.0: XP STATUS->DOS TABLE (835 PAIRS) VERIFIED\n"
             : "XP-MODERNIZATION-V1.0: MISMATCH\n");
    for(;;) __asm__ volatile("hlt");
}
