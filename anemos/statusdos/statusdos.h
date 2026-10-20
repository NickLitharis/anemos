/* STATUS -> DOS error map — Anemos's own replacement for the lookup XP's
 * ntdll!RtlNtStatusToDosError performs.
 *
 * The table is not transcribed from Microsoft's source (banned by SCOPE.md);
 * it holds answers *observed from XP's behaviour*. A guest-side probe
 * (mv_probe.c) calls the shipped ntdll export for all 2^32 possible NTSTATUS
 * values, compares each answer with this model, and writes back only the
 * disagreements as a certificate (check_cert.py); learn.py folds those into
 * model.txt. The guest and this table are two observations of one oracle,
 * and the certificate is what proves they agree.
 *
 * The API deliberately matches XP's export shape — ULONG-returning, total,
 * no failure mode — because that is the ABI any later swap must present.
 *
 * Lookup applies XP's rules in XP's order, then an exact-match table (sorted
 * ascending, binary search). See statusdos.c for the rules and their
 * provenance.
 */
#ifndef ANEMOS_STATUSDOS_H
#define ANEMOS_STATUSDOS_H

#include <stdint.h>

/* Total: every int32_t status maps to one Win32 error code. */
uint32_t anem_status_to_dos_error(int32_t status);

#endif /* ANEMOS_STATUSDOS_H */
