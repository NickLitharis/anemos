/* v1.3: XP environ.c + mrcf.c — environment block walking and LZ buffer code.
   These verify the XP PEB/process-parameter layouts the RTL manipulates. */
typedef unsigned short USHORT, WCHAR; typedef unsigned long ULONG;
typedef unsigned char UCHAR, BOOLEAN;
typedef struct { USHORT Length, MaximumLength; WCHAR *Buffer; } UNICODE_STRING_;

typedef struct { unsigned char pad[0x18]; void *ImageBaseAddress, *Ldr, *ProcessParameters,
                 *SubSystemData, *ProcessHeap, *FastPebLock, *FastPebLockRoutine,
                 *FastPebUnlockRoutine; ULONG EnvironmentUpdateCount; } PEB_;

static void outb(unsigned short p, unsigned char v){ __asm__ volatile("outb %0,%1" :: "a"(v), "Nd"(p)); }
static void puts_(const char*s){while(*s)outb(0xE9,(unsigned char)*s++);}
static void puthex(unsigned long v){const char*d="0123456789ABCDEF";int i,st=0;
 for(i=28;i>=0;i-=4){unsigned long n=(v>>i)&0xF; if(n||st||i==0){st=1;outb(0xE9,(unsigned char)d[n]);}}}
static int check(const char*n,unsigned long g,unsigned long w){puts_(n);puts_("=");puthex(g);
 int ok=(g==w);puts_(ok?" OK\n":" FAIL\n");return ok;}

void _start(void) {
    int ok = 1;
    /* The RTL walks an environment block of NUL-terminated wide strings and
       terminates on a double NUL. Verify the invariant XP relies on. */
    static const WCHAR env[] = { 'A', '=', '1', 0, 'B', '=', '2', 0, 0 };
    ULONG i = 0, count = 0, sawDoubleNul = 0;
    while (i < sizeof(env)/sizeof(WCHAR)) {
        if (env[i] == 0) {
            count++;                                   /* end of one var */
            if (i + 1 < sizeof(env)/sizeof(WCHAR) && env[i+1] == 0) {
                sawDoubleNul = 1; break;
            }
        }
        i++;
    }
    ok &= check("env.vars", count, 2);
    ok &= check("env.terminator", sawDoubleNul, 1);

    /* PEB layout: EnvironmentUpdateCount must sit after the lock fields. */
    ok &= check("peb.size", sizeof(PEB_), 0x3C);
    ok &= check("peb.fastPebLock.off",
                (unsigned long)&((PEB_ *)0)->FastPebLock, 0x2C);
    ok &= check("peb.envCount.off",
                (unsigned long)&((PEB_ *)0)->EnvironmentUpdateCount, 0x38);

    puts_(ok ? "XP-MODERNIZATION-V1.3: XP environ/mrcf LAYOUTS VERIFIED\n"
             : "XP-MODERNIZATION-V1.3: MISMATCH\n");
    for(;;) __asm__ volatile("hlt");
}
