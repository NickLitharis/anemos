/* Vector suite for anemos/statusdos, run under Wine against the raw probe
 * dump as its oracle.
 *
 * Two independent layers:
 *   - anchors: published Windows status -> error pairs, hardcoded here, not
 *     read from the dump — they catch a dump that is consistently wrong;
 *   - every run boundary in the dump (first-1, first, last), which is where
 *     a binary search off-by-one or a table-generation gap always shows.
 *
 * The exhaustive 2^32 check against the dump is verify.c (native, run.sh
 * step 4) — too slow to ask of Wine, and this file never claims it ran.
 *
 * usage: test_statusdos.exe <path to sdos.bin>   (Z:\-style or POSIX,
 *                                                  Windows accepts '/')
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "statusdos.h"
#include "statusdos.c.h"

/* Published, independent of the dump: the same anchors gen_table.py enforces
 * at generation time, stated again here so the test is not its own oracle. */
static const struct { int32_t status; uint32_t dos; } k_anchors[] = {
    { 0x00000000, 0 },     /* STATUS_SUCCESS          -> ERROR_SUCCESS */
    { (int32_t)0xc0000005, 998 },  /* ACCESS_VIOLATION  -> ERROR_NOACCESS */
    { (int32_t)0xc0000017, 8 },    /* NO_MEMORY         -> ERROR_NOT_ENOUGH_MEMORY */
    { (int32_t)0xc0000022, 5 },    /* ACCESS_DENIED     -> ERROR_ACCESS_DENIED */
    { (int32_t)0xc0000023, 122 },  /* BUFFER_TOO_SMALL  -> ERROR_INSUFFICIENT_BUFFER */
    { (int32_t)0xc0000034, 2 },    /* NAME_NOT_FOUND    -> ERROR_FILE_NOT_FOUND */
    { (int32_t)0xc000003a, 2 },    /* PATH_NOT_FOUND    -> ERROR_FILE_NOT_FOUND */
    { (int32_t)0xc000000d, 87 },   /* INVALID_PARAMETER -> ERROR_INVALID_PARAMETER */
    { (int32_t)0xc000013a, 1223 }, /* CONTROL_C_EXIT    -> ERROR_OPERATION_ABORTED */
};

struct run {
    uint32_t first;
    uint32_t count;
    uint32_t dos;
};

static struct run *g_runs;
static uint32_t g_nruns;

static bool load_dump(const char *path)
{
    FILE *f = fopen(path, "rb");
    long size;
    uint32_t i;
    uint32_t covered = 0;

    if (f == NULL) {
        printf("FAIL cannot open dump %s\n", path);
        return false;
    }
    fseek(f, 0, SEEK_END);
    size = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (size <= 0 || size % 12 != 0) {
        printf("FAIL dump size %ld not a whole number of triples\n", size);
        fclose(f);
        return false;
    }
    g_nruns = (uint32_t)(size / 12);
    g_runs = malloc((size_t)size);
    if (g_runs == NULL || fread(g_runs, 12, g_nruns, f) != g_nruns) {
        printf("FAIL dump read\n");
        fclose(f);
        return false;
    }
    fclose(f);
    if (g_runs[0].first != 0) {
        printf("FAIL dump does not start at status 0\n");
        return false;
    }
    for (i = 0; i < g_nruns; i++) {
        if (g_runs[i].first != covered || g_runs[i].count == 0) {
            printf("FAIL dump gap/overlap at 0x%08x\n", covered);
            return false;
        }
        covered += g_runs[i].count;
    }
    if (covered != 0u) {                    /* 2^32 wraps to 0 in uint32_t */
        printf("FAIL dump covers 0x%x runs-worth of statuses\n", covered);
        return false;
    }
    return true;
}

static bool check_structure(void)
{
    bool ok = true;
    uint32_t i;

    ok = (k_run_count >= 2) && ok;          /* a map with one run is broken */
    ok = (k_run_count == g_nruns) && ok;
    ok = (k_runs[0].first == 0) && ok;
    for (i = 1; ok && i < k_run_count; i++) {
        if (k_runs[i].first <= k_runs[i - 1].first) {
            printf("FAIL table not ascending at run %u\n", i);
            ok = false;
        }
    }
    printf("%s table structure (%u runs, matches dump)\n",
           ok ? "PASS" : "FAIL", k_run_count);
    return ok;
}

static bool check_anchors(void)
{
    bool ok = true;
    size_t i;

    for (i = 0; i < sizeof k_anchors / sizeof k_anchors[0]; i++) {
        uint32_t got = anem_status_to_dos_error(k_anchors[i].status);
        if (got != k_anchors[i].dos) {
            printf("FAIL anchor 0x%08x: got %u, published %u\n",
                   (uint32_t)k_anchors[i].status, got, k_anchors[i].dos);
            ok = false;
        }
    }
    printf("%s published anchors (%zu)\n", ok ? "PASS" : "FAIL",
           sizeof k_anchors / sizeof k_anchors[0]);
    return ok;
}

/* Every run boundary, both sides, plus the run's last status: this is where
 * binary search and table generation break, and it is exhaustive over the
 * table rather than a sample. */
static bool check_boundaries(void)
{
    bool ok = true;
    uint32_t i;

    for (i = 0; ok && i < g_nruns; i++) {
        uint32_t first = g_runs[i].first;
        uint32_t count = g_runs[i].count;
        uint32_t dos = g_runs[i].dos;

        if (anem_status_to_dos_error((int32_t)first) != dos) {
            printf("FAIL run %u first 0x%08x\n", i, first);
            ok = false;
        }
        if (anem_status_to_dos_error((int32_t)(first + count - 1u)) != dos) {
            printf("FAIL run %u last 0x%08x\n", i, first + count - 1u);
            ok = false;
        }
        if (first > 0) {
            if (anem_status_to_dos_error((int32_t)(first - 1u)) != g_runs[i - 1].dos) {
                printf("FAIL run %u predecessor 0x%08x\n", i, first - 1u);
                ok = false;
            }
        }
    }
    printf("%s run boundaries (every run: first, last, predecessor)\n",
           ok ? "PASS" : "FAIL");
    return ok;
}

int main(int argc, char **argv)
{
    unsigned failures = 0;

    if (argc != 2) {
        printf("usage: %s <sdos.bin>\n", argv[0]);
        return 2;
    }
    if (!load_dump(argv[1])) {
        return 1;
    }
    if (!check_structure()) { failures++; }
    if (!check_anchors()) { failures++; }
    if (!check_boundaries()) { failures++; }
    free(g_runs);

    if (failures != 0) {
        printf("FAILED: %u check(s)\n", failures);
        return 1;
    }
    printf("all checks pass\n");
    return 0;
}
