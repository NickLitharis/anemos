/* Internal shape of the generated table (table.c — gen_table.py output).
 * Not installed; statusdos.h is the unit's interface. */
#ifndef ANEMOS_STATUSDOS_C_H
#define ANEMOS_STATUSDOS_C_H

#include <stdint.h>

/* Run i covers [k_runs[i].first, k_runs[i+1].first) of the 2^32 status
 * space; the last run ends at 2^32. k_runs is ascending in .first, starts
 * at 0 — gen_table.py refuses to emit anything else. */
struct AnemStatusDosRun {
    uint32_t first;
    uint32_t dos;
};

extern const struct AnemStatusDosRun k_runs[];
extern const uint32_t k_run_count;

#endif /* ANEMOS_STATUSDOS_C_H */
