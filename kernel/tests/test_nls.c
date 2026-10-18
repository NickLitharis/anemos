/* v1.5: XP nls.c Unicode string routines - case mapping, compare, hash, append. */
typedef unsigned short WCHAR, USHORT; typedef unsigned long ULONG, LONG;
typedef long NTSTATUS; typedef unsigned char BOOLEAN, UCHAR;
typedef struct { USHORT Length, MaximumLength; WCHAR *Buffer; } UNICODE_STRING_;

WCHAR RtlUpcaseUnicodeChar(WCHAR c);
WCHAR RtlDowncaseUnicodeChar(WCHAR c);
LONG RtlCompareUnicodeString(const UNICODE_STRING_ *a, const UNICODE_STRING_ *b, BOOLEAN ci);
BOOLEAN RtlEqualUnicodeString(const UNICODE_STRING_ *a, const UNICODE_STRING_ *b, BOOLEAN ci);
NTSTATUS RtlHashUnicodeString(const UNICODE_STRING_ *s, BOOLEAN ci, ULONG alg, ULONG *hash);
void RtlCopyUnicodeString(UNICODE_STRING_ *dst, const UNICODE_STRING_ *src);
NTSTATUS RtlAppendUnicodeToString(UNICODE_STRING_ *dst, const WCHAR *src);
NTSTATUS RtlAppendUnicodeStringToString(UNICODE_STRING_ *dst, const UNICODE_STRING_ *src);

static void outb(unsigned short p, unsigned char v){ __asm__ volatile("outb %0,%1" :: "a"(v), "Nd"(p)); }
static void puts_(const char*s){while(*s)outb(0xE9,(unsigned char)*s++);}
static void puthex(unsigned long v){const char*d="0123456789ABCDEF";int i,st=0;
 for(i=28;i>=0;i-=4){unsigned long n=(v>>i)&0xF; if(n||st||i==0){st=1;outb(0xE9,(unsigned char)d[n]);}}}
static int check(const char*n,unsigned long g,unsigned long w){puts_(n);puts_("=");puthex(g);
 int ok=(g==w);puts_(ok?" OK\n":" FAIL\n");return ok;}
static void mk(UNICODE_STRING_ *u, WCHAR *buf, const char *s){
    USHORT n=0; while(s[n]){ buf[n]=(WCHAR)(unsigned char)s[n]; n++; } buf[n]=0;
    u->Buffer=buf; u->Length=(USHORT)(n*2); u->MaximumLength=(USHORT)((n+1)*2);
}

static WCHAR b1[32], b2[32], b3[64];

void _start(void) {
    int ok = 1;
    UNICODE_STRING_ u1, u2, u3;

    /* case mapping */
    ok &= check("upcase.a", RtlUpcaseUnicodeChar('a'), 'A');
    ok &= check("upcase.Z", RtlUpcaseUnicodeChar('Z'), 'Z');
    ok &= check("downcase.A", RtlDowncaseUnicodeChar('A'), 'a');
    ok &= check("downcase.5", RtlDowncaseUnicodeChar('5'), '5');

    mk(&u1, b1, "Hello");
    mk(&u2, b2, "hello");
    ok &= check("cmp.ci", (unsigned long)RtlCompareUnicodeString(&u1, &u2, 1), 0);
    ok &= check("cmp.cs.nonzero", (unsigned long)(RtlCompareUnicodeString(&u1, &u2, 0) != 0), 1);
    ok &= check("equal.ci", RtlEqualUnicodeString(&u1, &u2, 1), 1);
    ok &= check("equal.cs", RtlEqualUnicodeString(&u1, &u2, 0), 0);

    /* ordering: "abc" < "abd" */
    { UNICODE_STRING_ a, b; WCHAR x[8], y[8];
      mk(&a, x, "abc"); mk(&b, y, "abd");
      ok &= check("cmp.lt", (unsigned long)(RtlCompareUnicodeString(&a, &b, 0) != 0), 1); }

    /* hash must be deterministic and case-fold */
    { ULONG h1=0, h2=0;
      ok &= check("hash.status", (unsigned long)RtlHashUnicodeString(&u1, 1, 1, &h1), 0);
      ok &= check("hash.status2", (unsigned long)RtlHashUnicodeString(&u2, 1, 1, &h2), 0);
      ok &= check("hash.sameOnCI", h1, h2); }

    /* copy then append */
    /* destination needs room: set MaximumLength to its buffer size */
    u3.Buffer = b3; u3.MaximumLength = (USHORT)sizeof(b3); u3.Length = 0;
    RtlCopyUnicodeString(&u3, &u1);
    ok &= check("pre.len", u1.Length, 10);
    ok &= check("copy.len", u3.Length, 10);
    ok &= check("copy.eq", RtlEqualUnicodeString(&u3, &u1, 0), 1);
    { static const WCHAR tail[] = { '!', 0 };
      ok &= check("append.status", (unsigned long)RtlAppendUnicodeToString(&u3, tail), 0);
      ok &= check("append.len", u3.Length, 12);
      ok &= check("append.char", u3.Buffer[5], '!'); }
    { UNICODE_STRING_ w; WCHAR z[8]; mk(&w, z, "XYZ");
      ok &= check("append2.status", (unsigned long)RtlAppendUnicodeStringToString(&u3, &w), 0);
      ok &= check("append2.len", u3.Length, 18); }

    puts_(ok ? "XP-MODERNIZATION-V1.5: XP nls.c STRING ROUTINES VERIFIED\n"
             : "XP-MODERNIZATION-V1.5: MISMATCH\n");
    for(;;) __asm__ volatile("hlt");
}
