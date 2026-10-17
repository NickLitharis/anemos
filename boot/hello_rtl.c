/* v0.2 boot probe: execute REAL XP SP1 source (base/ntos/rtl/byteswap.c)
   inside the bootable image and verify its results. */
typedef unsigned short USHORT;
typedef unsigned long  ULONG;
typedef unsigned long long ULONGLONG;

/* defined by boot/rtl/byteswap_standalone.c — unmodified XP routine bodies.
   These are FASTCALL in the XP source, so the prototypes must say so; a cdecl
   prototype would pass the argument on the stack and read garbage from %cx. */
USHORT __attribute__((fastcall)) RtlUshortByteSwap(USHORT Source);
ULONG  __attribute__((fastcall)) RtlUlongByteSwap(ULONG Source);
ULONGLONG __attribute__((fastcall)) RtlUlonglongByteSwap(ULONGLONG Source);

static void outb(unsigned short port, unsigned char v) {
    __asm__ volatile("outb %0,%1" :: "a"(v), "Nd"(port));
}
static void puts_(const char *s) { while (*s) outb(0xE9, (unsigned char)*s++); }
static void puthex(unsigned long v) {
    const char *d = "0123456789ABCDEF";
    int i; int started = 0;
    for (i = 28; i >= 0; i -= 4) {
        unsigned long n = (v >> i) & 0xF;
        if (n || started || i == 0) { started = 1; outb(0xE9, (unsigned char)d[n]); }
    }
}
static int hexeq(unsigned long a, unsigned long b) { return a == b; }

void _start(void) {
    int ok = 1;

    /* RtlUshortByteSwap(0x1234) must be 0x3412 */
    unsigned long r16 = RtlUshortByteSwap(0x1234);
    puts_("RtlUshortByteSwap(1234)="); puthex(r16);
    ok &= hexeq(r16, 0x3412);
    puts_(ok ? " OK\n" : " FAIL\n");

    /* RtlUlongByteSwap(0x12345678) must be 0x78563412 */
    unsigned long r32 = RtlUlongByteSwap(0x12345678UL);
    puts_("RtlUlongByteSwap(12345678)="); puthex(r32);
    int ok32 = hexeq(r32, 0x78563412UL);
    ok &= ok32;
    puts_(ok32 ? " OK\n" : " FAIL\n");

    /* RtlUlonglongByteSwap: 0x1122334455667788 -> 0x8877665544332211,
       so the low 32 bits are 0x44332211 */
    ULONGLONG r64 = RtlUlonglongByteSwap(0x1122334455667788ULL);
    unsigned long lo = (unsigned long)r64;
    puts_("RtlUlonglongByteSwap(..55667788) low32="); puthex(lo);
    int ok64 = hexeq(lo, 0x44332211UL);
    ok &= ok64;
    puts_(ok64 ? " OK\n" : " FAIL\n");

    puts_(ok ? "XP-MODERNIZATION-V0.2: XP SP1 RTL CODE EXECUTED UNDER QEMU\n"
             : "XP-MODERNIZATION-V0.2: XP RTL MISMATCH\n");
    for (;;) __asm__ volatile("hlt");
}
