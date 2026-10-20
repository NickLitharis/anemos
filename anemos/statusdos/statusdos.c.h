/* Generated-table interface shared by statusdos.c, the in-guest probe, and
 * the generators. Do not edit the generated side by hand.
 *
 * k_entries is an exact-match exception list over the rules in statusdos.c:
 * each entry says "this one status maps to this DOS error", overriding both
 * the default and the customer rule. Entries are ascending and unique.
 *
 * k_entries_crc32 is CRC-32 over the raw little-endian (status, dos) pairs.
 * The in-guest probe stamps it into every certificate it writes, which is
 * how the host side detects a certificate produced from an older table and
 * refuses to treat it as evidence for the current one.
 */
#ifndef ANEMOS_STATUSDOS_C_H
#define ANEMOS_STATUSDOS_C_H

#include <stdint.h>

struct AnemStatusDosEntry {
    uint32_t status;
    uint32_t dos;
};

extern const struct AnemStatusDosEntry k_entries[];
extern const uint32_t k_entry_count;
extern const uint32_t k_entries_crc32;

#endif /* ANEMOS_STATUSDOS_C_H */
