/* Base64 — RFC 4648 §4 (standard alphabet A-Za-z0-9+/, '=' padding).
 *
 * XP's CryptoAPI exports CryptBinaryToStringA/W and CryptStringToBinaryA/W
 * from crypt32.dll (verified on this build's export table); those are the
 * eventual target ABI, not a contract yet — crypt32 is not rebuildable in
 * P1 (see PLAN.md). XP remains the oracle for output values: expected
 * strings in test_base64.c were generated with Python's base64, an
 * independent implementation, and the first seven are RFC 4648 §10's own
 * published vectors.
 *
 * Contract:
 *   - encode and decode are all-or-nothing: on any error the output buffer
 *     is not modified and *written is not modified;
 *   - decode is strict per RFC 4648 §3.2-3.5 — input length is a multiple
 *     of four (padding included, so unpadded input is rejected), the
 *     alphabet only (no whitespace), '=' only in the final group and at
 *     most two, and the unused low bits of the last data character must be
 *     zero;
 *   - no allocation; the caller owns every buffer (OS-CODING-RULES §1);
 *   - a zero-length operation may pass out == NULL with out_cap == 0.
 *
 * shortcut: whitespace and newlines are rejected outright; if XP's decoder
 * accepts them, the differential will show it and this becomes a flag.
 */
#ifndef ANEMOS_BASE64_H
#define ANEMOS_BASE64_H

#include <stddef.h>
#include <stdint.h>

#include "../include/anem_status.h"

/* Exact encoded length for in_len input bytes (4 * ceil(in_len / 3)). */
AnemStatus anem_b64_encoded_size(size_t in_len, size_t *out_len);

/* `out` needs at least anem_b64_encoded_size(len) bytes. */
AnemStatus anem_b64_encode(const void *data, size_t len,
                           char *out, size_t out_cap, size_t *written);

/* `out` needs at most (len / 4) * 3 bytes. `text` is counted, not
 * NUL-terminated; NUL-terminated callers pass strlen(text). */
AnemStatus anem_b64_decode(const char *text, size_t len,
                           void *out, size_t out_cap, size_t *written);

#endif /* ANEMOS_BASE64_H */
