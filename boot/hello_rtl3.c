/* v0.5: exercise XP string.c routines (RtlInitUnicodeStringEx, RtlCompareString,
   RtlEqualString, RtlUpperChar) linked from kernel/out/libxprtl.a. */
typedef unsigned short WCHAR, USHORT;
typedef unsigned long ULONG;
typedef long NTSTATUS;
typedef struct { USHORT Length, MaximumLength; WCHAR *Buffer; } UNICODE_STRING_;

NTSTATUS RtlInitUnicodeStringEx(UNICODE_STRING_ *Dest, const WCHAR *Src);
long RtlCompareString(const UNICODE_STRING_ *a, const UNICODE_STRING_ *b, unsigned char ci);
unsigned char RtlEqualString(const UNICODE_STRING_ *a, const UNICODE_STRING_ *b, unsigned char ci);
char RtlUpperChar(char c);

static void outb(unsigned short p, unsigned char v) { __asm__ volatile("outb %0,%1" :: "a"(v), "Nd"(p)); }
static void puts_(const char *s) { while (*s) outb(0xE9, (unsigned char)*s++); }
static void puthex(unsigned long v) {
    const char *d = "0123456789ABCDEF"; int i, st = 0;
    for (i = 28; i >= 0; i -= 4) { unsigned long n = (v >> i) & 0xF;
        if (n || st || i == 0) { st = 1; outb(0xE9, (unsigned char)d[n]); } }
}
static int check(const char *n, unsigned long got, unsigned long want) {
    puts_(n); puts_("="); puthex(got);
    int ok = (got == want); puts_(ok ? " OK\n" : " FAIL\n"); return ok;
}
static void wcpy(WCHAR *d, const char *s) { while (*s) *d++ = (WCHAR)(unsigned char)*s++; *d = 0; }

void _start(void) {
    int ok = 1;
    WCHAR b1[16], b2[16];
    UNICODE_STRING_ u1, u2;

    wcpy(b1, "Hello");
    wcpy(b2, "hello");

    ok &= check("InitUnicodeStringEx", (unsigned long)RtlInitUnicodeStringEx(&u1, b1), 0);
    ok &= check("u1.Length", u1.Length, 10);
    RtlInitUnicodeStringEx(&u2, b2);

    /* case-insensitive compare of "Hello" vs "hello" is 0 (equal) */
    ok &= check("CompareString(ci)", (unsigned long)RtlCompareString(&u1, &u2, 1), 0);
    /* case-sensitive equal is FALSE */
    ok &= check("EqualString(cs)", RtlEqualString(&u1, &u2, 0), 0);
    ok &= check("EqualString(ci)", RtlEqualString(&u1, &u2, 1), 1);

    /* RtlUpperChar from XP string.c */
    ok &= check("UpperChar(a)", (unsigned long)(unsigned char)RtlUpperChar('a'), 'A');
    ok &= check("UpperChar(Z)", (unsigned long)(unsigned char)RtlUpperChar('Z'), 'Z');

    puts_(ok ? "XP-MODERNIZATION-V0.5: XP string.c ROUTINES EXECUTED AND VERIFIED\n"
             : "XP-MODERNIZATION-V0.5: MISMATCH\n");
    for (;;) __asm__ volatile("hlt");
}
