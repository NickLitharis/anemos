/* sdos_probe — runs inside the XP guest, once.
 *
 * Clean-room extraction of ntdll!RtlNtStatusToDosError: XP is the oracle and
 * this observes its behaviour for every one of the 2^32 possible NTSTATUS
 * values without ever reading Microsoft's table. The output is a
 * run-length-encoded stream of (first_status, count, dos_error) triples,
 * little-endian; when complete the runs tile [0, 2^32) exactly, which is how
 * the host side detects a truncated run.
 *
 * Freestanding on purpose: no CRT, imports only kernel32 + ntdll, so the
 * binary stays small enough to overwrite an expendable guest exe via
 * tools/ntfspatch.py and be typed at the Run-relative path by the runner.
 * FILE_FLAG_WRITE_THROUGH + FlushFileBuffers because a hard power cut must
 * not be able to lose what was already written.
 */
typedef void *HANDLE;

#define INVALID_HANDLE_VALUE     ((HANDLE)(long)-1)
#define GENERIC_WRITE            0x40000000ul
#define CREATE_ALWAYS            2ul
#define FILE_ATTRIBUTE_NORMAL    0x80ul
#define FILE_FLAG_WRITE_THROUGH  0x80000000ul

__declspec(dllimport) HANDLE __stdcall CreateFileA(const char *, unsigned long,
        unsigned long, void *, unsigned long, unsigned long, HANDLE);
__declspec(dllimport) unsigned long __stdcall WriteFile(HANDLE, const void *,
        unsigned long, unsigned long *, void *);
__declspec(dllimport) int __stdcall FlushFileBuffers(HANDLE);
__declspec(dllimport) int __stdcall CloseHandle(HANDLE);
__declspec(dllimport) void __stdcall ExitProcess(unsigned int);
__declspec(dllimport) long __stdcall RtlNtStatusToDosError(long);

static unsigned char g_buf[65536];
static unsigned long g_used;
static unsigned long g_runs;
static int g_failed;
static HANDLE g_out;

/* Cap: a pathologically fragmented dump must not fill the guest's C: — the
 * guest writes this file while running off that very disk. 2^25 triples =
 * ~381 MB; running out stops cleanly, and the host side reports exactly
 * where the tiling broke, which is also how the rate is measured. */
#define RUN_CAP 0x2000000ul

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

static void emit(unsigned long first, unsigned long count, unsigned long dos)
{
    unsigned char *p;
    unsigned i;
    if (g_used + 12 > sizeof g_buf) {
        flush_buf();
    }
    if (g_failed) {
        return;
    }
    p = g_buf + g_used;
    for (i = 0; i < 4; i++) {
        p[i]      = (unsigned char)(first >> (8 * i));
        p[4 + i]  = (unsigned char)(count >> (8 * i));
        p[8 + i]  = (unsigned char)(dos >> (8 * i));
    }
    g_used += 12;
    g_runs++;
}

void mainCRTStartup(void)
{
    unsigned long run_first = 0;
    unsigned long run_count = 1;
    unsigned long prev;
    unsigned long long s;

    g_out = CreateFileA("C:\\conftest\\sdos.bin", GENERIC_WRITE, 0, 0,
                        CREATE_ALWAYS,
                        FILE_ATTRIBUTE_NORMAL | FILE_FLAG_WRITE_THROUGH,
                        INVALID_HANDLE_VALUE);
    if (g_out == INVALID_HANDLE_VALUE) {
        ExitProcess(1);
    }

    prev = (unsigned long)RtlNtStatusToDosError(0);
    for (s = 1; s < 0x100000000ull; s++) {          /* 2^32 statuses */
        unsigned long dos =
            (unsigned long)RtlNtStatusToDosError((long)(unsigned long)s);
        if (dos != prev) {
            emit(run_first, run_count, prev);
            run_first = (unsigned long)s;
            run_count = 0;
            prev = dos;
        }
        run_count++;
        if (g_failed) {
            ExitProcess(2);
        }
        if (g_runs == RUN_CAP) {
            /* stop cleanly rather than fill the disk: the partial dump is
             * still a valid prefix, and the host will say where it ended */
            flush_buf();
            FlushFileBuffers(g_out);
            CloseHandle(g_out);
            ExitProcess(3);
        }
    }
    emit(run_first, run_count, prev);
    flush_buf();
    FlushFileBuffers(g_out);
    CloseHandle(g_out);
    ExitProcess(g_failed ? 2 : 0);
}
