/* STATUS -> DOS error lookup. See statusdos.h for the contract. */
#include "statusdos.h"
#include "statusdos.c.h"

uint32_t anem_status_to_dos_error(int32_t status)
{
    /* Cast: two's-complement reinterpretation over the full 32-bit range —
     * unsigned ordering is exactly the order we want to binary-search. */
    uint32_t s = (uint32_t)status;
    uint32_t lo = 0;
    uint32_t hi = k_run_count;

    while (lo < hi) {
        uint32_t mid = lo + (hi - lo) / 2u;
        if (k_runs[mid].first <= s) {
            lo = mid + 1u;
        } else {
            hi = mid;
        }
    }
    /* lo = first run starting beyond s; the containing run is lo - 1.
     * k_runs[0].first == 0 <= s for every s, so lo >= 1 — and if a bad
     * table ever broke that invariant, returning 0 (ERROR_SUCCESS) is the
     * only answer that cannot be worse than reading k_runs[-1]. */
    if (lo == 0) {
        return 0;
    }
#ifdef ANEM_STATUSDOS_NEGATIVE
    /* Off by one run — the classic binary-search bug, kept only so the
     * suite's negative control has something true to fail against. */
    return k_runs[lo > 1 ? lo - 2 : lo - 1].dos;
#else
    return k_runs[lo - 1].dos;
#endif
}
