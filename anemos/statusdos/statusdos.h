/* STATUS -> DOS error map — Anemos's own replacement for the lookup XP's
 * ntdll!RtlNtStatusToDosError performs.
 *
 * The table is not transcribed from Microsoft's source (banned by SCOPE.md);
 * it was extracted clean-room from XP's *behaviour*: a guest-side probe
 * (sdos_probe.c) called the shipped ntdll export for every one of the 2^32
 * possible NTSTATUS values and recorded what it returned, run-length
 * encoded. The guest and this table are two observations of the same oracle.
 *
 * The API deliberately matches XP's export shape — ULONG-returning, total,
 * no failure mode — because that is the ABI any later swap must present.
 *
 * Lookup is a binary search over ascending run starts; run i covers
 * [k_runs[i].first, k_runs[i+1].first), and the last run ends at 2^32.
 */
#ifndef ANEMOS_STATUSDOS_H
#define ANEMOS_STATUSDOS_H

#include <stdint.h>

/* Total: every int32_t status maps to one Win32 error code. */
uint32_t anem_status_to_dos_error(int32_t status);

#endif /* ANEMOS_STATUSDOS_H */
