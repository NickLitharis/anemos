/* POSIX stand-ins for the Win32 platform block of mv_probe.c, used only by
 * the ANEM_MV_NATIVE build (selftest.sh). See the note at the top of
 * mv_probe.c: the point is that the walk, the emit and the footer are the
 * *same source* in the self-test and in the guest probe — one writer of the
 * certificate format cannot drift from the one reader (check_cert.py).
 *
 * Behaviour notes, so the self-test's expectations are on record:
 *   ExitProcess(code)  -> exit(code), so the walk's outcome (0 = clean,
 *                         3 = mismatches, 4 = capped, 1/2 = I/O failure) is
 *                         assertable from the shell.
 *   RtlNtStatusToDosError -> fake_xp() (selftest_oracle.c), the synthetic
 *                         oracle the convergence loop must recover.
 *   CreateFileA        -> writes to $ANEM_MV_CERT (default sdos-cert.bin in
 *                         the cwd): the guest path "C:\conftest\..." is not
 *                         a path a Linux test run should be creating.
 *
 * Never linked into anything shipped.
 */
#ifndef ANEMOS_STATUSDOS_SELFTEST_SHIM_H
#define ANEMOS_STATUSDOS_SELFTEST_SHIM_H

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

typedef FILE *HANDLE;

#define INVALID_HANDLE_VALUE     ((HANDLE)NULL)
#define GENERIC_WRITE            0x40000000ul
#define CREATE_ALWAYS            2ul
#define FILE_ATTRIBUTE_NORMAL    0x80ul
#define FILE_FLAG_WRITE_THROUGH  0x80000000ul

void mainCRTStartup(void);          /* defined in mv_probe.c below */
uint32_t fake_xp(uint32_t s);       /* defined in selftest_oracle.c */

static inline HANDLE CreateFileA(const char *path, unsigned long access,
        unsigned long share, void *sec, unsigned long disp,
        unsigned long flags, HANDLE template_file)
{
    const char *out = getenv("ANEM_MV_CERT");
    (void)path; (void)access; (void)share; (void)sec;
    (void)disp; (void)flags; (void)template_file;
    return fopen(out ? out : "sdos-cert.bin", "wb");
}

static inline int CreateDirectoryA(const char *path, void *sec)
{
    (void)path; (void)sec;
    return 1;                       /* the guest path is ignored; see above */
}

static inline int WriteFile(HANDLE f, const void *buf, unsigned long len,
        unsigned long *wrote, void *ovl)
{
    (void)ovl;
    *wrote = (unsigned long)fwrite(buf, 1, len, f);
    return *wrote == len;
}

static inline int FlushFileBuffers(HANDLE f) { return fflush(f) == 0; }
static inline int CloseHandle(HANDLE f)      { return fclose(f) == 0; }

/* The probe's heartbeat goes to the guest console; natively, stderr, so a
 * self-test run prints the same 16 counters without polluting its output. */
#define STD_OUTPUT_HANDLE 0ul
static inline HANDLE GetStdHandle(unsigned long which)
{
    (void)which;
    return stderr;
}
static inline void ExitProcess(unsigned int code) { exit((int)code); }

/* The oracle swap: where the guest build calls ntdll, the self-test calls
 * the synthetic one. One line — this is the only difference in behaviour
 * between a walk in the guest and a walk on the host. */
static inline long RtlNtStatusToDosError(long status)
{
    return (long)fake_xp((uint32_t)status);
}

/* Declared before its definition, exactly as mv_probe.c does for
 * mainCRTStartup, so -Wmissing-prototypes has nothing to say. */
int main(void);

int main(void)
{
    mainCRTStartup();
    return 0;                       /* unreachable: ExitProcess exits */
}

#endif /* ANEMOS_STATUSDOS_SELFTEST_SHIM_H */
