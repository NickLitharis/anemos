/* A fixed synthetic oracle for the learning-loop self-test (selftest.sh).
 *
 * It answers exactly like the current model (it *calls* the model), except
 * at K planted statuses, where it returns a different value — standing in
 * for the table entries the real XP has and our model has not learned yet.
 *
 * The function is defined once and behaves the same in both walks of the
 * self-test. First walk, against the unlearned model: exactly the K planted
 * mismatches must be found. learn.py folds them in. Second walk, against the
 * learned model: zero mismatches. That is the whole convergence loop, proven
 * without a VM — the certificate format, the checker's refusals, the
 * learner's rewrite, and the regenerated table, all exercised end to end.
 *
 * This file is never linked into anything shipped. It is a test fixture.
 */
#include <stdint.h>

#include "statusdos.h"       /* anem_status_to_dos_error — the model */

static const struct { uint32_t status; uint32_t dos; } k_planted[] = {
    { 0x00000500u, 5u },      /* quadrant 0: low, non-customer */
    { 0x40000005u, 2u },      /* quadrant 1: informational */
    { 0x80000005u, 122u },    /* quadrant 2: warning */
    { 0xC0000200u, 87u },     /* quadrant 3: error */
    { 0xC0000201u, 998u },    /* quadrant 3: error, adjacent to the above */
};

/* Prototype here too: this file is compiled on its own, without the shim
 * header that declares it for mv_probe.c. */
uint32_t fake_xp(uint32_t s);

/* The synthetic XP answer: the model, plus the entries it has not learned. */
uint32_t fake_xp(uint32_t s)
{
    unsigned i;
    for (i = 0; i < sizeof k_planted / sizeof k_planted[0]; i++) {
        if (k_planted[i].status == s) {
            return k_planted[i].dos;
        }
    }
    return anem_status_to_dos_error((int32_t)s);
}
