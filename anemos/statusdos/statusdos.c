/* STATUS -> DOS error lookup. See statusdos.h for the contract.
 *
 * The model is six rules plus a learned exact-match table, applied in XP's
 * order (Source/XPSP1/NT/base/ntos/rtl/error.c is the specification; every
 * rule is *confirmed* against XP itself by the in-guest probe, mv_probe.c):
 *
 *   1. customer bit (0x20000000) set   -> the status itself;
 *   2. 0x80070000 .. 0x8007FFFF        -> the low 16 bits (a Win32 error
 *                                         already carried in an NTSTATUS);
 *   3. 0xD0000000 .. 0xDFFFFFFF        -> clear 0x30000000 and keep looking
 *                                         (an HRESULT wrapping a status);
 *   4. exact-match table hit           -> that DOS error;
 *   5. 0xC0010000 .. 0xC001FFFF        -> the low 16 bits (a redirector-
 *                                         mangled OS/2 error);
 *   6. otherwise                       -> 317 (ERROR_MR_MID_NOT_FOUND).
 *
 * Rule 1 is a BIT TEST, not a range. XP answers identity for every status
 * with bit 29 set, whatever the severity bits say: the mv-20261019-222631
 * walk observed it 426,590 times, including the whole 0x60000000-0x7FFFFFFF
 * band. The first version of this model used the range
 * [0x20000000,0x40000000) — which is what the old raw dump had actually
 * covered — and was wrong for every 0x60000000+ status. That is exactly the
 * failure mode model-verification exists to catch: once the rule became a
 * bit test, the next walk found 9 mismatches left in quadrant 1 and 70 in
 * quadrant 2, all of them table entries.
 *
 * Rules 2, 3 and 5 were hypotheses until the certificate backed them: the
 * walk compares all 2^32 statuses, so a wrong rule cannot survive a VERIFIED
 * certificate, and a rule-shaped divergence still shows up as mismatches
 * (check_cert.py says so, and refuses to call it a missing table entry).
 * The certificate at cert/sdos-cert.bin is that verdict: zero mismatches.
 *
 * Table entries are *observations* of XP, never transcriptions: learn.py
 * folds them in from a certificate, and no entry may sit where a rule
 * already decides the answer (gen_model.py rejects those as redundant or
 * unreachable).
 */
#include "statusdos.h"
#include "statusdos.c.h"

#define ANEM_STATUSDOS_DEFAULT 317u       /* ERROR_MR_MID_NOT_FOUND */
#define ANEM_STATUSDOS_CUSTOMER 0x20000000u
#define ANEM_STATUSDOS_WIN32_HI 0x80070000u
#define ANEM_STATUSDOS_WIN32_MASK 0xffff0000u
#define ANEM_STATUSDOS_HRESULT_HI 0xd0000000u
#define ANEM_STATUSDOS_HRESULT_MASK 0xf0000000u
#define ANEM_STATUSDOS_HRESULT_CLEAR 0xcfffffffu
#define ANEM_STATUSDOS_OS2_HI 0xc001u

/* Index of the exact entry for s, or k_entry_count if absent. Found-ness is
 * the index itself, never the dos value: 317 is a legitimate mapping, so a
 * sentinel chosen from the value space would conflate "maps to 317" with
 * "not in the table" and silently misroute the former through the rules. */
static uint32_t table_find(uint32_t s)
{
    uint32_t lo = 0;
    uint32_t hi = k_entry_count;

    while (lo < hi) {
        uint32_t mid = lo + (hi - lo) / 2u;
        if (k_entries[mid].status < s) {
            lo = mid + 1u;
        } else {
            hi = mid;
        }
    }
    return (lo < k_entry_count && k_entries[lo].status == s) ? lo : k_entry_count;
}

uint32_t anem_status_to_dos_error(int32_t status)
{
    /* Cast: two's-complement reinterpretation over the full 32-bit range —
     * the table and every rule are stated in unsigned terms. */
    uint32_t s = (uint32_t)status;
    uint32_t idx;

    if ((s & ANEM_STATUSDOS_CUSTOMER) != 0u) {
        return s;
    }
    if ((s & ANEM_STATUSDOS_WIN32_MASK) == ANEM_STATUSDOS_WIN32_HI) {
        return s & 0xffffu;
    }
    if ((s & ANEM_STATUSDOS_HRESULT_MASK) == ANEM_STATUSDOS_HRESULT_HI) {
        s &= ANEM_STATUSDOS_HRESULT_CLEAR;
    }

    idx = table_find(s);

#ifdef ANEM_STATUSDOS_NEGATIVE
    /* Off by one — the classic binary-search bug: consult the entry before
     * the one found. Kept only so the suite's negative control has something
     * true to fail against. */
    if (idx > 0 && idx <= k_entry_count) {
        idx--;
    }
#endif
    if (idx < k_entry_count) {
        return k_entries[idx].dos;
    }
    if ((s >> 16) == ANEM_STATUSDOS_OS2_HI) {
        return s & 0xffffu;
    }
    return ANEM_STATUSDOS_DEFAULT;
}
