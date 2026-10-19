/* The gate: anem_status_to_dos_error vs the XP guest's raw answers, for
 * every one of the 2^32 statuses. Native build, run by run.sh step 4 — a
 * binary search against its own table proves nothing, so the oracle here is
 * the dump, and the expected side walks runs sequentially (monotonic) while
 * the impl binary-searches: two algorithms that must agree everywhere.
 *
 * Exits 0 only if all 2^32 comparisons hold; the first mismatch prints and
 * stops, so the negative control (run.sh step 5) fails fast.
 *
 * usage: verify <sdos.bin>
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "statusdos.h"

struct run {
    uint32_t first;
    uint32_t count;
    uint32_t dos;
};

int main(int argc, char **argv)
{
    FILE *f;
    long size;
    uint32_t nruns, i, covered = 0, idx = 0;
    struct run *runs;
    uint64_t s;

    if (argc != 2) {
        printf("usage: %s <sdos.bin>\n", argv[0]);
        return 2;
    }
    f = fopen(argv[1], "rb");
    if (f == NULL) {
        printf("FAIL cannot open %s\n", argv[1]);
        return 1;
    }
    fseek(f, 0, SEEK_END);
    size = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (size <= 0 || size % 12 != 0) {
        printf("FAIL dump size %ld\n", size);
        return 1;
    }
    nruns = (uint32_t)(size / 12);
    runs = malloc((size_t)size);
    if (runs == NULL || fread(runs, 12, nruns, f) != nruns) {
        printf("FAIL dump read\n");
        return 1;
    }
    fclose(f);

    if (runs[0].first != 0) {
        printf("FAIL dump does not start at 0\n");
        return 1;
    }
    for (i = 0; i < nruns; i++) {
        if (runs[i].first != covered || runs[i].count == 0) {
            printf("FAIL dump gap/overlap at 0x%08x\n", covered);
            return 1;
        }
        covered += runs[i].count;           /* wraps to 0 iff full 2^32 */
    }
    if (covered != 0) {
        printf("FAIL dump covers 0x%x, not the full 2^32\n", covered);
        return 1;
    }

    for (s = 0; s < 0x100000000ull; s++) {
        /* Sequential expected-side: each run's end is checked as s walks
         * past it; never re-searched, so no shared bug with the impl. */
        while (idx + 1 < nruns && s >= (uint64_t)runs[idx].first + runs[idx].count) {
            idx++;
        }
        uint32_t got = anem_status_to_dos_error((int32_t)(uint32_t)s);
        if (got != runs[idx].dos) {
            printf("FAIL status 0x%08llx: impl %u, XP %u (run %u)\n",
                   (unsigned long long)s, got, runs[idx].dos, idx);
            return 1;
        }
    }
    printf("all 4294967296 statuses match XP (%u runs)\n", nruns);
    return 0;
}
