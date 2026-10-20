/* mv_probe — the model-verification probe. Runs inside the XP guest, once.
 *
 * The rework of the raw-dump extraction (docs/P1-STATUSDOS.md): instead of
 * recording all 2^32 answers (~GB, and the customer region alone is >6 GB),
 * this walks all 2^32 statuses, compares the *shipping* implementation
 * (statusdos.c + the generated table, linked in below) against XP's own
 * ntdll!RtlNtStatusToDosError, and writes back only the disagreements.
 * Output is kilobytes; the write load that bugchecked the guest once before
 * is now two file creates and a few flushes.
 *
 * The certificate it writes is self-describing and checkable host-side:
 *   header  "SDMV" + version + entry_count + CRC32(k_entries)
 *   records (status, ours, xp) triples, little-endian, capped per quadrant
 *   footer  "SDMX" + total mismatches + per-quadrant totals + capped mask
 *           + statuses checked (must be 2^32)
 * A certificate whose header CRC does not match the table the host is
 * currently testing was produced from an older table and is refused: the
 * "no stale table can back the suite" property, extended to certificates.
 *
 * Recording is capped per quadrant (top 2 bits of the status), not globally:
 * a catastrophically wrong rule in one region must not hide what the other
 * three regions say. Counting never stops — only recording does — so the
 * footer's totals are always complete even when records were dropped.
 *
 * Freestanding on purpose: no CRT, imports only kernel32 + ntdll, so the
 * binary fits an expendable guest exe (see run.sh for the swap recipe).
 * FILE_FLAG_WRITE_THROUGH + FlushFileBuffers because a hard power cut must
 * not be able to lose what was already written.
 *
 * The ANEM_MV_NATIVE build swaps only the platform block below for POSIX
 * stand-ins (selftest_shim.h), so the SAME walk, emit and footer code — the
 * certificate format — compiles natively for selftest.sh. There is exactly
 * one writer of the format in this tree; a second hand-copied writer could
 * drift and make a green self-test prove nothing about the real probe.
 */
#ifdef ANEM_MV_NATIVE
#include "selftest_shim.h"
#else
typedef void *HANDLE;

#define INVALID_HANDLE_VALUE     ((HANDLE)(long)-1)
#define GENERIC_WRITE            0x40000000ul
#define CREATE_ALWAYS            2ul
#define FILE_ATTRIBUTE_NORMAL    0x80ul
#define FILE_FLAG_WRITE_THROUGH  0x80000000ul

__declspec(dllimport) HANDLE __stdcall CreateFileA(const char *, unsigned long,
        unsigned long, void *, unsigned long, unsigned long, HANDLE);
__declspec(dllimport) int __stdcall CreateDirectoryA(const char *, void *);
__declspec(dllimport) unsigned long __stdcall WriteFile(HANDLE, const void *,
        unsigned long, unsigned long *, void *);
__declspec(dllimport) int __stdcall FlushFileBuffers(HANDLE);
__declspec(dllimport) HANDLE __stdcall GetStdHandle(unsigned long);
__declspec(dllimport) int __stdcall CloseHandle(HANDLE);

#define STD_OUTPUT_HANDLE ((unsigned long)-11)
__declspec(dllimport) void __stdcall ExitProcess(unsigned int);
__declspec(dllimport) long __stdcall RtlNtStatusToDosError(long);
#endif /* ANEM_MV_NATIVE */

#include "statusdos.h"       /* anem_status_to_dos_error — the shipping code */
#include "statusdos.c.h"     /* k_entries, k_entry_count, k_entries_crc32 */

/* Per-quadrant record cap: 2^18 triples = 3 MB, 12 MB worst case total.
 * A guest whose C: fills stops being a probe and becomes a BSOD. */
#define QUAD_CAP   0x40000ul
#define REC_SIZE   12ul
#define HDR_SIZE   16ul
/*   0  "SDMX"
 *   4  u64 total mismatches
 *  12  u64 quadrant 0 mismatches   (status >> 30 == 0)
 *  20  u64 quadrant 1 mismatches
 *  28  u64 quadrant 2 mismatches
 *  36  u64 quadrant 3 mismatches
 *  44  u32 capped quadrant mask
 *  48  u64 statuses checked (must be exactly 2^32) */
#define FTR_SIZE   56ul

static unsigned char g_buf[65536];
static unsigned long g_used;
static unsigned long g_quad_rec[4];     /* records written per quadrant */
static unsigned long long g_quad_mis[4];/* true mismatches per quadrant */
static int g_failed;
static int g_capped;
static HANDLE g_out;

/* The PE entry point, pointed at by the linker's -e flag: not called from
 * anywhere in this file, so declare it to satisfy -Wmissing-prototypes. */
void mainCRTStartup(void);

static void flush_buf(void)
{
    unsigned long wrote = 0;
    if (g_failed || g_used == 0) {
        return;
    }
    if (!WriteFile(g_out, g_buf, g_used, &wrote, 0) || wrote != g_used) {
        g_failed = 1;
    }
    g_used = 0;
}

/* One heartbeat line per 2^28 statuses (16 for the whole walk), carrying the
 * count reached. A walk that outlives the runner's settle window used to be
 * silent — indistinguishable from a hung probe; now its pace is on the
 * console. Costs 16 WriteFiles, which is nothing next to the 2^32 calls. */
static void progress(unsigned long long s)
{
    char line[20];
    unsigned long wrote = 0;
    unsigned i = 0;
    unsigned j;

    for (j = 0; j < 16; j++) {
        unsigned long digit = (unsigned long)((s >> (4 * (15 - j))) & 0xFul);
        line[i++] = (char)(digit < 10 ? '0' + digit : 'a' + (digit - 10));
    }
    line[i++] = '\r';
    line[i++] = '\n';
    WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), line, i, &wrote, 0);
}

static void put32(unsigned char *p, unsigned long v)
{
    unsigned i;
    for (i = 0; i < 4; i++) {
        p[i] = (unsigned char)(v >> (8 * i));
    }
}

static void put64(unsigned char *p, unsigned long long v)
{
    unsigned i;
    for (i = 0; i < 8; i++) {
        p[i] = (unsigned char)(v >> (8 * i));
    }
}

static void emit(unsigned long status, unsigned long ours, unsigned long xp)
{
    unsigned char *p;
    unsigned long quad = status >> 30;

    g_quad_mis[quad]++;
    if (g_quad_rec[quad] == QUAD_CAP) {
        /* stop recording this quadrant, keep counting everywhere */
        g_capped = 1;
        return;
    }
    if (g_used + REC_SIZE > sizeof g_buf) {
        flush_buf();
    }
    if (g_failed) {
        return;
    }
    p = g_buf + g_used;
    put32(p, status);
    put32(p + 4, ours);
    put32(p + 8, xp);
    g_used += REC_SIZE;
    g_quad_rec[quad]++;
}

void mainCRTStartup(void)
{
    unsigned char hdr[HDR_SIZE];
    unsigned char ftr[FTR_SIZE];
    unsigned long wrote = 0;
    unsigned long long s;
    unsigned long long total = 0;
    unsigned i;

    g_out = CreateFileA("C:\\conftest\\sdos-cert.bin", GENERIC_WRITE, 0, 0,
                        CREATE_ALWAYS,
                        FILE_ATTRIBUTE_NORMAL | FILE_FLAG_WRITE_THROUGH,
                        INVALID_HANDLE_VALUE);
    if (g_out == INVALID_HANDLE_VALUE) {
        /* Most likely \conftest does not exist yet on a fresh guest: create
         * it and retry once. CreateDirectoryA failing on an existing
         * directory is expected and ignored — the retry is the real test. */
        CreateDirectoryA("C:\\conftest", 0);
        g_out = CreateFileA("C:\\conftest\\sdos-cert.bin", GENERIC_WRITE, 0, 0,
                            CREATE_ALWAYS,
                            FILE_ATTRIBUTE_NORMAL | FILE_FLAG_WRITE_THROUGH,
                            INVALID_HANDLE_VALUE);
        if (g_out == INVALID_HANDLE_VALUE) {
            ExitProcess(1);
        }
    }

    /* Header: magic, version, the table this walk is judged against. */
    put32(hdr, 0x564d4453ul);            /* "SDMV" little-endian */
    put32(hdr + 4, 1ul);                 /* certificate version */
    put32(hdr + 8, k_entry_count);
    put32(hdr + 12, k_entries_crc32);
    if (!WriteFile(g_out, hdr, HDR_SIZE, &wrote, 0) || wrote != HDR_SIZE) {
        ExitProcess(2);
    }

    for (s = 0; s < 0x100000000ull; s++) {          /* 2^32 statuses */
        unsigned long status = (unsigned long)s;
        unsigned long ours = anem_status_to_dos_error((int32_t)status);
        unsigned long xp =
            (unsigned long)RtlNtStatusToDosError((long)(unsigned long)status);
        if ((s & 0x0FFFFFFFull) == 0) {
            progress(s);
        }
        if (ours != xp) {
            emit(status, ours, xp);
            total++;
        }
        if (g_failed) {
            ExitProcess(2);
        }
    }

    flush_buf();

    /* Footer: magic, complete totals, what was recorded vs cut, and proof
     * the walk covered every status (a truncated walk must never be
     * mistakable for a complete one). */
    put32(ftr, 0x584d4453ul);            /* "SDMX" little-endian */
    put64(ftr + 4, total);
    for (i = 0; i < 4; i++) {
        put64(ftr + 12 + 8 * i, g_quad_mis[i]);
    }
    {
        unsigned long mask = 0;
        for (i = 0; i < 4; i++) {
            if (g_quad_mis[i] > g_quad_rec[i]) {
                mask |= 1ul << i;
            }
        }
        put32(ftr + 44, mask);
    }
    put64(ftr + 48, 0x100000000ull);
    if (!WriteFile(g_out, ftr, FTR_SIZE, &wrote, 0) || wrote != FTR_SIZE) {
        ExitProcess(2);
    }
    FlushFileBuffers(g_out);
    CloseHandle(g_out);
    ExitProcess(g_failed ? 2 : (g_capped ? 4 : (total != 0 ? 3 : 0)));
}
