/* v1.4: XP sertl.c security routines - SID construction, length, copy, compare. */
typedef unsigned char UCHAR, BYTE, BOOLEAN; typedef unsigned short USHORT;
typedef unsigned long ULONG, ACCESS_MASK; typedef long NTSTATUS; typedef void *PSID;
typedef struct { BYTE Value[6]; } SID_IDENTIFIER_AUTHORITY, *PSID_IDENTIFIER_AUTHORITY;

NTSTATUS RtlInitializeSid(PSID Sid, PSID_IDENTIFIER_AUTHORITY Authority, UCHAR Count);
ULONG RtlLengthSid(PSID Sid);
ULONG RtlLengthRequiredSid(ULONG SubAuthorityCount);
NTSTATUS RtlCopySid(ULONG DestLen, PSID Dest, PSID Source);
BOOLEAN RtlEqualSid(PSID a, PSID b);
BOOLEAN RtlEqualPrefixSid(PSID a, PSID b);
BOOLEAN RtlValidSid(PSID Sid);
ULONG *RtlSubAuthoritySid(PSID Sid, ULONG i);
UCHAR *RtlSubAuthorityCountSid(PSID Sid);
BOOLEAN RtlAreAllAccessesGranted(ACCESS_MASK granted, ACCESS_MASK desired);
BOOLEAN RtlAreAnyAccessesGranted(ACCESS_MASK granted, ACCESS_MASK desired);

static void outb(unsigned short p, unsigned char v){ __asm__ volatile("outb %0,%1" :: "a"(v), "Nd"(p)); }
static void puts_(const char*s){while(*s)outb(0xE9,(unsigned char)*s++);}
static void puthex(unsigned long v){const char*d="0123456789ABCDEF";int i,st=0;
 for(i=28;i>=0;i-=4){unsigned long n=(v>>i)&0xF; if(n||st||i==0){st=1;outb(0xE9,(unsigned char)d[n]);}}}
static int check(const char*n,unsigned long g,unsigned long w){puts_(n);puts_("=");puthex(g);
 int ok=(g==w);puts_(ok?" OK\n":" FAIL\n");return ok;}

static unsigned char sid1[68], sid2[68], sid3[68];
static const SID_IDENTIFIER_AUTHORITY NT_AUTH = {{0,0,0,0,0,5}};

void _start(void) {
    int ok = 1;
    NTSTATUS st;

    st = RtlInitializeSid(sid1, (PSID_IDENTIFIER_AUTHORITY)&NT_AUTH, 2);
    ok &= check("init.status", (unsigned long)st, 0);
    ok &= check("subAuthCount", *RtlSubAuthorityCountSid(sid1), 2);
    /* SID header is 8 bytes + 4 per subauthority */
    ok &= check("length", RtlLengthSid(sid1), 8 + 2 * 4);
    ok &= check("lengthRequired", RtlLengthRequiredSid(2), 8 + 2 * 4);
    ok &= check("valid", RtlValidSid(sid1), 1);

    *RtlSubAuthoritySid(sid1, 0) = 32;   /* BUILTIN\Administrators */
    *RtlSubAuthoritySid(sid1, 1) = 544;
    ok &= check("sub0", *RtlSubAuthoritySid(sid1, 0), 32);
    ok &= check("sub1", *RtlSubAuthoritySid(sid1, 1), 544);

    st = RtlCopySid(sizeof(sid2), sid2, sid1);
    ok &= check("copy.status", (unsigned long)st, 0);
    ok &= check("copy.equal", RtlEqualSid(sid1, sid2), 1);

    /* different authority -> not equal */
    {
        static const SID_IDENTIFIER_AUTHORITY WORLD = {{0,0,0,0,0,1}};
        RtlInitializeSid(sid3, (PSID_IDENTIFIER_AUTHORITY)&WORLD, 2);
        *RtlSubAuthoritySid(sid3, 0) = 32;
        *RtlSubAuthoritySid(sid3, 1) = 544;
        ok &= check("diffAuthority", RtlEqualSid(sid1, sid3), 0);
    }

    /* access masks */
    ok &= check("allGranted", RtlAreAllAccessesGranted(0xF, 0x7), 1);
    ok &= check("allNotGranted", RtlAreAllAccessesGranted(0x3, 0x7), 0);
    ok &= check("anyGranted", RtlAreAnyAccessesGranted(0x3, 0xC), 0);
    ok &= check("anyGranted2", RtlAreAnyAccessesGranted(0x5, 0x4), 1);

    puts_(ok ? "XP-MODERNIZATION-V1.4: XP sertl.c SID ROUTINES VERIFIED\n"
             : "XP-MODERNIZATION-V1.4: MISMATCH\n");
    for(;;) __asm__ volatile("hlt");
}
