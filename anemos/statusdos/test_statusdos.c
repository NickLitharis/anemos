/* Vector + boundary suite for anemos/statusdos, run under Wine.
 *
 * The model is six rules plus an exact-match exception table (statusdos.c),
 * so this suite checks the layers separately:
 *   - anchors: published Windows status -> error pairs, hardcoded here and
 *     in gen_model.py — they catch a model that is consistently wrong;
 *   - rules: documented probe statuses with absolute expected values, at
 *     every boundary where a rule turns on or off — including the ones the
 *     first model got wrong (the customer bit is a bit test, not a range;
 *     0x60000000+ is identity, and mv-20261019-222631 is what proved it);
 *   - entry boundaries: every table entry at status-1, status and status+1,
 *     where a binary-search off-by-one or a redundant entry always shows.
 *
 * The exhaustive checks are elsewhere and this file never claims they ran:
 * the prefix differential (all statuses the old raw dump covers, native) is
 * verify_prefix.c; the full 2^32 comparison against live XP is the in-guest
 * probe (mv_probe.c) plus check_cert.py.
 *
 * usage: test_statusdos.exe     (no arguments)
 */
#include <stdio.h>
#include <stdbool.h>

#include "statusdos.h"
#include "statusdos.c.h"

#define DEFAULT 317u

/* Published, independent of the model: the same anchors gen_model.py enforces
 * at generation time, restated so the test is not its own oracle. */
static const struct { int32_t status; uint32_t dos; } k_anchors[] = {
    { 0x00000000, 0 },     /* STATUS_SUCCESS          -> ERROR_SUCCESS */
    { (int32_t)0xc000000d, 87 },   /* INVALID_PARAMETER -> ERROR_INVALID_PARAMETER */
    { (int32_t)0xc0000005, 998 },  /* ACCESS_VIOLATION  -> ERROR_NOACCESS */
    { (int32_t)0xc0000017, 8 },    /* NO_MEMORY         -> ERROR_NOT_ENOUGH_MEMORY */
    { (int32_t)0xc0000022, 5 },    /* ACCESS_DENIED     -> ERROR_ACCESS_DENIED */
    { (int32_t)0xc0000023, 122 },  /* BUFFER_TOO_SMALL  -> ERROR_INSUFFICIENT_BUFFER */
    { (int32_t)0xc0000034, 2 },    /* NAME_NOT_FOUND    -> ERROR_FILE_NOT_FOUND */
    { (int32_t)0xc000003a, 3 },    /* PATH_NOT_FOUND    -> ERROR_PATH_NOT_FOUND */
    { (int32_t)0xc000013a, 317 },  /* CONTROL_C_EXIT    -> NOT in XP's table:
                                    * the walk says ERROR_MR_MID_NOT_FOUND,
                                    * not the documented 1223 */
};

/* The rules of statusdos.c restated independently: a divergence between this
 * and the shipping function is a real bug in one of them, which is the point
 * of stating it twice. Table lookups here are exact-match on the status rule
 * 3 leaves behind, exactly as the shipping code searches it. */
static const struct AnemStatusDosEntry *entry_for(uint32_t s)
{
    uint32_t lo = 0, hi = k_entry_count;

    while (lo < hi) {
        uint32_t mid = lo + (hi - lo) / 2u;
        if (k_entries[mid].status < s) { lo = mid + 1u; } else { hi = mid; }
    }
    return (lo < k_entry_count && k_entries[lo].status == s) ? &k_entries[lo] : NULL;
}

static uint32_t rule_answer(uint32_t s)
{
    const struct AnemStatusDosEntry *e;

    if ((s & 0x20000000u) != 0u) {
        return s;                              /* customer bit: identity */
    }
    if ((s & 0xffff0000u) == 0x80070000u) {
        return s & 0xffffu;                    /* Win32 error in an NTSTATUS */
    }
    if ((s & 0xf0000000u) == 0xd0000000u) {
        s &= 0xcfffffffu;                      /* HRESULT wrapping a status */
    }
    e = entry_for(s);
    if (e != NULL) {
        return e->dos;
    }
    if ((s >> 16) == 0xc001u) {
        return s & 0xffffu;                    /* redirector-mangled OS/2 */
    }
    return DEFAULT;
}

static bool check_table_structure(void)
{
    bool ok = true;
    uint32_t i;

    ok = (k_entry_count >= 1) && ok;
    for (i = 1; ok && i < k_entry_count; i++) {
        if (k_entries[i].status <= k_entries[i - 1].status) {
            printf("FAIL table not strictly ascending at entry %u "
                   "(0x%08x after 0x%08x)\n",
                   i, k_entries[i].status, k_entries[i - 1].status);
            ok = false;
        }
    }
    printf("%s table structure (%u entries, ascending)\n",
           ok ? "PASS" : "FAIL", k_entry_count);
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

/* Absolute expected values, not derived from the model: each is either XP's
 * published algorithm or an answer an in-guest walk actually observed. A
 * table entry may legitimately sit on a rule's status (it then wins), so a
 * probe that lands on one says so instead of quietly passing. */
static bool check_rules(void)
{
    static const struct { uint32_t status; uint32_t want; const char *why; }
    probes[] = {
        { 0x1fffffffu, DEFAULT, "last status below the customer bit" },
        { 0x20000000u, 0x20000000u, "customer bit alone: identity" },
        { 0x3fffffffu, 0x3fffffffu, "identity, end of the old dump's range" },
        { 0x40000000u, DEFAULT, "informational, no customer bit" },
        { 0x60000000u, 0x60000000u,
          "customer bit SET: identity (the walk's 262,135-record quadrant)" },
        { 0x7fffffffu, 0x7fffffffu, "customer bit set at the quadrant top" },
        { 0x80000000u, DEFAULT, "warning, no customer bit" },
        { 0x80070000u, 0x00000000u, "Win32-error passthrough, low 16 = 0" },
        { 0x80071234u, 0x00001234u, "Win32-error passthrough, mid-block" },
        { 0x8007ffffu, 0x0000ffffu, "Win32-error passthrough, block top" },
        { 0x80080000u, DEFAULT, "one past the passthrough block" },
        { 0xc0010005u, 0x00000005u, "redirector-mangled OS/2, low 16" },
        { 0xc0020005u, DEFAULT, "one block past the OS/2 rule" },
        { 0xc8000000u, DEFAULT, "error severity, no customer bit" },
        { 0xffffffffu, 0xffffffffu,
          "customer bit set at the top of the space: identity wins over severity" },
    };
    bool ok = true;
    size_t i;

    for (i = 0; i < sizeof probes / sizeof probes[0]; i++) {
        uint32_t s = probes[i].status;
        uint32_t got = anem_status_to_dos_error((int32_t)s);
        const struct AnemStatusDosEntry *e = entry_for(s);

        if (e != NULL) {
            printf("note: 0x%08x has a table entry; its rule probe is "
                   "covered by the entry boundary check\n", s);
            continue;
        }
        if (got != probes[i].want) {
            printf("FAIL rule 0x%08x (%s): got %u, want %u\n",
                   s, probes[i].why, got, probes[i].want);
            ok = false;
        }
    }

    /* Rule 3 cannot be probed with one absolute value: it must answer exactly
     * what the unwrapped status answers, whatever the table says. */
    {
        uint32_t hijacked = anem_status_to_dos_error((int32_t)0xd0000005u);
        uint32_t plain = anem_status_to_dos_error((int32_t)0xc0000005u);
        if (hijacked != plain) {
            printf("FAIL rule 0xD0000005 (HRESULT unwrap): got %u, but "
                   "0xC0000005 answers %u\n", hijacked, plain);
            ok = false;
        }
    }

    printf("%s rule probes (%zu)\n", ok ? "PASS" : "FAIL",
           sizeof probes / sizeof probes[0] + 1);
    return ok;
}

/* Every table entry at status-1, status and status+1: the three statuses a
 * binary search off-by-one or an off-by-one entry boundary gets wrong. */
static bool check_entries(void)
{
    bool ok = true;
    uint32_t i;

    for (i = 0; ok && i < k_entry_count; i++) {
        uint32_t s = k_entries[i].status;
        uint32_t dos = k_entries[i].dos;

        if (anem_status_to_dos_error((int32_t)s) != dos) {
            printf("FAIL entry %u: 0x%08x should map to %u\n", i, s, dos);
            ok = false;
        }
        if (s > 0) {
            uint32_t before = anem_status_to_dos_error((int32_t)(s - 1u));
            uint32_t want = rule_answer(s - 1u);
            if (before != want) {
                printf("FAIL entry %u: 0x%08x (predecessor) got %u want %u\n",
                       i, s - 1u, before, want);
                ok = false;
            }
        }
        {
            uint32_t after = anem_status_to_dos_error((int32_t)(s + 1u));
            uint32_t want = rule_answer(s + 1u);
            if (after != want) {
                printf("FAIL entry %u: 0x%08x (successor) got %u want %u — "
                       "entries must be exact-match, not runs\n",
                       i, s + 1u, after, want);
                ok = false;
            }
        }
    }
    printf("%s entry boundaries (every entry: status-1, status, status+1)\n",
           ok ? "PASS" : "FAIL");
    return ok;
}

int main(void)
{
    unsigned failures = 0;

    if (!check_table_structure()) { failures++; }
    if (!check_anchors()) { failures++; }
    if (!check_rules()) { failures++; }
    if (!check_entries()) { failures++; }

    if (failures != 0) {
        printf("FAILED: %u check(s)\n", failures);
        return 1;
    }
    printf("all checks pass\n");
    return 0;
}
