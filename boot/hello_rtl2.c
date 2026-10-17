/* v0.3: exercise more real XP SP1 RTL code under QEMU:
   intbits.c  -> RtlInterlockedSetBits / ClearBits / SetClearBits
   checksum.c -> RtlComputeCrc32
   slistfunc.c-> RtlpInitializeSListHead / RtlpQueryDepthSList
*/
typedef unsigned char UCHAR; typedef unsigned short USHORT;
typedef unsigned long ULONG, *PULONG; typedef unsigned int ULONG32;

ULONG __attribute__((fastcall)) RtlInterlockedSetBits(PULONG Flags, ULONG Flag);
ULONG __attribute__((fastcall)) RtlInterlockedClearBits(PULONG Flags, ULONG Flag);
ULONG __attribute__((fastcall)) RtlInterlockedSetClearBits(PULONG Set, PULONG Clear);
typedef unsigned char *PUCHAR_;
ULONG32 RtlComputeCrc32(ULONG32 PartialCrc, PUCHAR_ Buffer, ULONG Length);

typedef struct _SLIST_HEADER_S { unsigned long long Alignment; ULONG Depth; ULONG Sequence; } SLIST_H;
void RtlpInitializeSListHead(SLIST_H *h);
USHORT RtlpQueryDepthSList(SLIST_H *h);

static void outb(unsigned short p, unsigned char v) { __asm__ volatile("outb %0,%1" :: "a"(v), "Nd"(p)); }
static void puts_(const char *s) { while (*s) outb(0xE9, (unsigned char)*s++); }
static void puthex(unsigned long v) {
    const char *d = "0123456789ABCDEF"; int i, started = 0;
    for (i = 28; i >= 0; i -= 4) { unsigned long n = (v >> i) & 0xF;
        if (n || started || i == 0) { started = 1; outb(0xE9, (unsigned char)d[n]); } }
}
static int check(const char *name, unsigned long got, unsigned long want) {
    puts_(name); puts_("="); puthex(got);
    int ok = (got == want); puts_(ok ? " OK\n" : " FAIL\n"); return ok;
}

void _start(void) {
    int ok = 1;
    ULONG flags = 0;

    /* XP: RtlInterlockedSetBits returns the PREVIOUS value and sets the bit */
    ULONG r1 = RtlInterlockedSetBits(&flags, 0x00000010UL);
    ok &= check("SetBits.ret", r1, 0);
    ok &= check("SetBits.val", flags, 0x00000010UL);

    ULONG r2 = RtlInterlockedClearBits(&flags, 0x00000010UL);
    ok &= check("ClearBits.ret", r2, 0x00000010UL);
    ok &= check("ClearBits.val", flags, 0);

    /* CRC32 of "123456789" is the standard 0xCBF43926 */
    unsigned char buf[9]; const char *src = "123456789"; int i;
    for (i = 0; i < 9; i++) buf[i] = (unsigned char)src[i];
    ULONG32 crc = RtlComputeCrc32(0, buf, 9);
    ok &= check("Crc32(123456789)", crc, 0xCBF43926UL);

    SLIST_H h; RtlpInitializeSListHead(&h);
    ok &= check("SList.depth", RtlpQueryDepthSList(&h), 0);

    puts_(ok ? "XP-MODERNIZATION-V0.3: 3 MORE XP RTL FILES EXECUTED UNDER QEMU\n"
             : "XP-MODERNIZATION-V0.3: RTL MISMATCH\n");
    for (;;) __asm__ volatile("hlt");
}
