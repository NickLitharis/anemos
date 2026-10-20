/* The host-side differential: anem_status_to_dos_error vs the XP guest's
 * recorded answers, for every status the prefix dump actually covers.
 *
 * The full 2^32 dump was proven structurally infeasible (docs/P1-STATUSDOS.md);
 * what survived from the one genuine run is a byte-perfect prefix — statuses
 * 0x0 .. ~0x2028669D, ~2.6M runs. That is still ~539 million real XP answers,
 * and every one of them is checked here, natively, in seconds.
 *
 * The dump's tail is known to be scrambled, so the walk stops at the first
 * structural violation (gap, overlap, zero-length run) and treats everything
 * beyond as absent — a truncated dump can under-claim coverage but can never
 * make the model pass against corrupted bytes.
 *
 * One record is excluded, and the exclusion can itself fail: the dump's last
 * triple is 0x2028669D -> 1, sitting exactly at the corruption boundary, and
 * the customer-bit rule says XP answers 0x2028669D (identity). The mv-
 * 20261019-222631 walk asked XP again and got identity, so that triple is
 * damage, not behaviour. It is dropped only when it is exactly that status
 * with exactly that value; any other disagreement still fails the run.
 *
 * This is NOT the full verification. The full 2^32 comparison against live
 * XP is the in-guest probe (mv_probe.c) plus check_cert.py; this file is the
 * cheap host-side half that runs on every suite invocation.
 *
 * Exits 0 only if every covered status matches; first mismatch prints, stops.
 *
 * usage: verify_prefix <sdos.bin>
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

/* The dump's final triple, proven corrupt: see the note at the top. */
#define DUMP_CORRUPT_TAIL 0x2028669du
#define DUMP_CORRUPT_VALUE 1u

static uint32_t dump_dos(const struct run *runs, uint32_t valid, uint32_t s)
{
    uint32_t idx = 0;

    while (idx + 1 < valid
           && s >= runs[idx].first + runs[idx].count) {
        idx++;
    }
    return runs[idx].dos;
}

int main(int argc, char **argv)
{
    FILE *f;
    long size;
    uint32_t nruns, i, covered = 0, idx = 0;
    struct run *runs;
    uint32_t valid = 0;
    uint64_t s, end;

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

    /* Find the valid prefix: runs that tile from 0 with no gap or zero run.
     * Everything from the first violation on is treated as absent. */
    if (runs[0].first != 0 || runs[0].count == 0) {
        printf("FAIL dump does not start at status 0\n");
        return 1;
    }
    covered = runs[0].count;
    valid = 1;
    for (i = 1; i < nruns; i++) {
        if (runs[i].first != covered || runs[i].count == 0) {
            printf("note: dump structure breaks at run %u (0x%08x != "
                   "0x%08x); checking the %u-run prefix only\n",
                   i, runs[i].first, covered, valid);
            break;
        }
        covered += runs[i].count;
        valid++;
    }
    end = covered;                       /* first unchecked status */
    if (end > 0) {
        uint32_t last = (uint32_t)(end - 1u);
        uint32_t got = anem_status_to_dos_error((int32_t)last);
        uint32_t dumped = dump_dos(runs, valid, last);
        if (got != dumped && last == DUMP_CORRUPT_TAIL
                && dumped == DUMP_CORRUPT_VALUE) {
            printf("note: dropping the dump's final triple 0x%08x -> %u: the "
                   "corruption boundary, contradicted by the in-guest walk\n",
                   last, dumped);
            end = last;
        }
    }
    printf("checking %llu statuses (0x0 .. 0x%08llx), %u runs\n",
           (unsigned long long)end, (unsigned long long)end - 1, valid);

    for (s = 0; s < end; s++) {
        while (idx + 1 < valid
               && s >= (uint64_t)runs[idx].first + runs[idx].count) {
            idx++;
        }
        uint32_t got = anem_status_to_dos_error((int32_t)(uint32_t)s);
        if (got != runs[idx].dos) {
            printf("FAIL status 0x%08llx: model %u, XP %u (run %u)\n",
                   (unsigned long long)s, got, runs[idx].dos, idx);
            return 1;
        }
    }
    printf("all %llu statuses in the dump's valid prefix match XP (%u runs)\n",
           (unsigned long long)end, valid);
    return 0;
}
