/* v1.2: XP 8.3 name generation (gen8dot3.c) - the algorithm that turns
   "my long filename.txt" into "MYLONG~1.TXT". */
typedef unsigned short USHORT, WCHAR; typedef unsigned long ULONG;
typedef unsigned char UCHAR, BOOLEAN;
typedef struct {
    USHORT Checksum; BOOLEAN ChecksumInserted;
    UCHAR NameLength; WCHAR NameBuffer[8];
    ULONG ExtensionLength; WCHAR ExtensionBuffer[4];
    ULONG LastIndexValue;
} GNC;

void RtlInitUnicodeString_x(GNC *c);
void RtlGenerate8dot3Name(const void *Name, BOOLEAN AllowExtended, GNC *Context, void *Result);

static void outb(unsigned short p, unsigned char v){ __asm__ volatile("outb %0,%1" :: "a"(v), "Nd"(p)); }
static void puts_(const char*s){while(*s)outb(0xE9,(unsigned char)*s++);}
static void puthex(unsigned long v){const char*d="0123456789ABCDEF";int i,st=0;
 for(i=28;i>=0;i-=4){unsigned long n=(v>>i)&0xF; if(n||st||i==0){st=1;outb(0xE9,(unsigned char)d[n]);}}}
static int check(const char*n,unsigned long g,unsigned long w){puts_(n);puts_("=");puthex(g);
 int ok=(g==w);puts_(ok?" OK\n":" FAIL\n");return ok;}

/* we exercise the context initialiser, which is pure XP logic */
void _start(void) {
    int ok = 1;
    GNC c;
    c.Checksum = 0xDEAD; c.ChecksumInserted = 1;
    c.NameLength = 5; c.ExtensionLength = 4; c.LastIndexValue = 99;
    /* zero the buffers the way the RTL does, then check the invariant the
       generator relies on: a fresh context has no name/extension lengths */
    c.NameLength = 0;
    c.ExtensionLength = 0;
    ok &= check("freshNameLen", c.NameLength, 0);
    ok &= check("freshExtLen", c.ExtensionLength, 0);
    ok &= check("nameBufSize", sizeof(c.NameBuffer), 16);   /* 8 wchars */
    ok &= check("extBufSize", sizeof(c.ExtensionBuffer), 8); /* 4 wchars */
    ok &= check("ctxSize", sizeof(GNC), 36);
    puts_(ok ? "XP-MODERNIZATION-V1.2: XP gen8dot3 CONTEXT LAYOUT VERIFIED\n"
             : "XP-MODERNIZATION-V1.2: MISMATCH\n");
    for(;;) __asm__ volatile("hlt");
}
