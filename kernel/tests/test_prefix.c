/* v0.9: XP prefix.c — a splay-tree based prefix lookup. */
typedef unsigned short USHORT, WCHAR; typedef unsigned long ULONG;
typedef short CSHORT; typedef long NTSTATUS; typedef unsigned char BOOLEAN;
typedef struct { USHORT Length, MaximumLength; WCHAR *Buffer; } UNICODE_STRING, *PUNICODE_STRING;
typedef struct _RTL_SPLAY_LINKS { struct _RTL_SPLAY_LINKS *Parent, *LeftChild, *RightChild; } RTL_SPLAY_LINKS;
typedef struct _UNICODE_PREFIX_TABLE_ENTRY {
    CSHORT NodeTypeCode; CSHORT NameLength;
    struct _UNICODE_PREFIX_TABLE_ENTRY *NextPrefixTree, *CaseMatch;
    RTL_SPLAY_LINKS Links; PUNICODE_STRING Prefix;
} UNICODE_PREFIX_TABLE_ENTRY, *PUNICODE_PREFIX_TABLE_ENTRY;
typedef struct _UNICODE_PREFIX_TABLE {
    CSHORT NodeTypeCode; CSHORT NameLength;
    PUNICODE_PREFIX_TABLE_ENTRY NextPrefixTree, LastNextEntry;
} UNICODE_PREFIX_TABLE, *PUNICODE_PREFIX_TABLE;

void RtlInitializeUnicodePrefix(PUNICODE_PREFIX_TABLE);
BOOLEAN RtlInsertUnicodePrefix(PUNICODE_PREFIX_TABLE, PUNICODE_STRING, PUNICODE_PREFIX_TABLE_ENTRY);
PUNICODE_PREFIX_TABLE_ENTRY RtlFindUnicodePrefix(PUNICODE_PREFIX_TABLE, PUNICODE_STRING, ULONG);

static void outb(unsigned short p, unsigned char v){ __asm__ volatile("outb %0,%1" :: "a"(v), "Nd"(p)); }
static void puts_(const char*s){while(*s)outb(0xE9,(unsigned char)*s++);}
static void puthex(unsigned long v){const char*d="0123456789ABCDEF";int i,st=0;
 for(i=28;i>=0;i-=4){unsigned long n=(v>>i)&0xF; if(n||st||i==0){st=1;outb(0xE9,(unsigned char)d[n]);}}}
static int check(const char*n,unsigned long g,unsigned long w){puts_(n);puts_("=");puthex(g);
 int ok=(g==w);puts_(ok?" OK\n":" FAIL\n");return ok;}
static void wset(WCHAR *d, const char *s, USHORT *len){USHORT i=0;while(s[i]){d[i]=(WCHAR)(unsigned char)s[i];i++;}d[i]=0;*len=(USHORT)(i*2);}

static UNICODE_PREFIX_TABLE pt;
static UNICODE_PREFIX_TABLE_ENTRY e1, e2, e3;
static WCHAR b1[8], b2[8], b3[8];
static UNICODE_STRING p1, p2, p3;

void _start(void) {
    int ok = 1;
    wset(b1, "AB", &p1.Length); p1.Buffer = b1; p1.MaximumLength = sizeof(b1);
    wset(b2, "ABC", &p2.Length); p2.Buffer = b2; p2.MaximumLength = sizeof(b2);
    wset(b3, "XY", &p3.Length); p3.Buffer = b3; p3.MaximumLength = sizeof(b3);

    RtlInitializeUnicodePrefix(&pt);

    puts_("e1="); puthex((unsigned long)&e1 & 0xFFFF); puts_(" e2="); puthex((unsigned long)&e2 & 0xFFFF); puts_("\n");
    puts_("about AB\n");
    ok &= check("insert AB", RtlInsertUnicodePrefix(&pt, &p1, &e1), 1);
    puts_("e1="); puthex((unsigned long)&e1 & 0xFFFF); puts_(" e2="); puthex((unsigned long)&e2 & 0xFFFF); puts_("\n");
    puts_("about AB\n");
    puts_("about ABC\n");
    ok &= check("insert ABC", RtlInsertUnicodePrefix(&pt, &p2, &e2), 1);
    puts_("about XY\n");
    ok &= check("insert XY", RtlInsertUnicodePrefix(&pt, &p3, &e3), 1);

    { PUNICODE_PREFIX_TABLE_ENTRY f = RtlFindUnicodePrefix(&pt, &p1, 0);
      ok &= check("find AB", (unsigned long)(f != 0), 1); }
    { PUNICODE_PREFIX_TABLE_ENTRY f = RtlFindUnicodePrefix(&pt, &p3, 0);
      ok &= check("find XY", (unsigned long)(f != 0), 1); }
    { UNICODE_STRING q; WCHAR bq[8]; wset(bq, "ZZ", &q.Length);
      q.Buffer = bq; q.MaximumLength = sizeof(bq);
      ok &= check("find ZZ", (unsigned long)(RtlFindUnicodePrefix(&pt, &q, 0) != 0), 0); }

    puts_(ok ? "XP-MODERNIZATION-V0.9: XP prefix.c EXECUTED AND VERIFIED\n"
             : "XP-MODERNIZATION-V0.9: MISMATCH\n");
    for(;;) __asm__ volatile("hlt");
}
