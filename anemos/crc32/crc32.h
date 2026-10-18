/* CRC-32 — the zlib / ITU V.42 flavour: reflected polynomial 0xEDB88320,
 * initial value 0xFFFFFFFF, final XOR 0xFFFFFFFF.
 *
 * XP exports no CRC-32 on this build (verified against ntoskrnl.exe's export
 * table: no RtlComputeCrc32 — that export arrived after XP), so there is no
 * XP ABI to match and no XP oracle here: correctness is pinned to published
 * vectors ("a" -> 0xE8B7BE43, "abc" -> 0x352441C2) plus Python's zlib, an
 * independent implementation, for the rest (see vectors.py).
 *
 * anem_crc32_update continues a chain from the value anem_crc32 would have
 * returned (zlib's convention), so continuing with an empty buffer returns
 * the running value unchanged.
 *
 * No allocation, no global state (the table is const), any function that can
 * fail says so with an AnemStatus (OS-CODING-RULES §4, §11).
 */
#ifndef ANEMOS_CRC32_H
#define ANEMOS_CRC32_H

#include <stddef.h>
#include <stdint.h>

#include "../include/anem_status.h"

AnemStatus anem_crc32(const void *data, size_t len, uint32_t *crc);

/* `running` is the previous result (of this function or of anem_crc32). */
AnemStatus anem_crc32_update(uint32_t running, const void *data, size_t len,
                             uint32_t *crc);

#endif /* ANEMOS_CRC32_H */
