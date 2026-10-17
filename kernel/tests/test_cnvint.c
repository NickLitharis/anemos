/* v0.6: verify XP cnvint.c integer/string conversion under QEMU.
   RtlIntegerToUnicodeString and RtlUnicodeStringToInteger are real conversion
   state machines, not constants. */
typedef unsigned short WCHAR, USHORT;
typedef unsigned long ULONG;
typedef long NTSTATUS;
typedef struct { USHORT Length, MaximumLength; WCHAR *Buffer; } US;

NTSTATUS RtlIntegerToUnicodeString(ULONG Value, ULONG Base, US *String);
NTSTATUS RtlUnicodeStringToInteger(const US *String, ULONG Base, ULONG *Value);

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
void _start(void) {
    int ok = 1;
    WCHAR buf[36];
    US s; s.Buffer = buf; s.MaximumLength = sizeof(buf); s.Length = 0;

    /* 255 -> decimal "255", length 6 bytes */
    ok &= check("IntegerToUnicode(255,10)", (unsigned long)RtlIntegerToUnicodeString(255, 10, &s), 0);
    ok &= check("len", s.Length, 6);
    ok &= check("buf0", s.Buffer[0], '2');
    ok &= check("buf2", s.Buffer[2], '5');

    /* 0xDEADBEEF -> hex, first char must be 'D' and length 16 bytes */
    s.Length = 0;
    ok &= check("IntegerToUnicode(DEADBEEF,16)", (unsigned long)RtlIntegerToUnicodeString(0xDEADBEEFUL, 16, &s), 0);
    ok &= check("hexlen", s.Length, 16);  /* 8 chars * 2 bytes = 0x10 */
    ok &= check("hex0", s.Buffer[0], 'D');
    ok &= check("hex3", s.Buffer[3], 'D');
    ok &= check("hex4", s.Buffer[4], 'B');
    ok &= check("hex7", s.Buffer[7], 'F');

    /* round-trip: parse "1234" back */
    WCHAR d[4]; int i; const char *src = "1234";
    for (i = 0; i < 4; i++) d[i] = (WCHAR)(unsigned char)src[i];
    US in; in.Buffer = d; in.Length = 8; in.MaximumLength = 8;
    ULONG val = 0;
    ok &= check("UnicodeToInteger", (unsigned long)RtlUnicodeStringToInteger(&in, 10, &val), 0);
    ok &= check("parsed", val, 1234);

    puts_(ok ? "XP-MODERNIZATION-V0.6: XP cnvint.c EXECUTED AND VERIFIED\n"
             : "XP-MODERNIZATION-V0.6: MISMATCH\n");
    for (;;) __asm__ volatile("hlt");
}
