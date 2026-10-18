/* NLS tables — XP's ANSI codepage (CP1252) and strict UTF-8 (RFC 3629).
 *
 * CP1252 is what an XP install with CP_ACP=1252 uses everywhere (this VM
 * included); its mapping is published by the Unicode Consortium, and the
 * tables here were generated from that mapping via an independent
 * implementation (Python's cp1252 codec — see vectors.py), so the suite does
 * not trust what it tests. Five bytes are undefined in CP1252 (0x81, 0x8d,
 * 0x8f, 0x90, 0x9d) and both directions reject them.
 *
 * UTF-8 here is strict RFC 3629: no overlong forms, no surrogates
 * (U+D800..U+DFFF), nothing above U+10FFFF, no 5- or 6-byte forms, no
 * truncated sequences.
 *
 * shortcut: XP's own MultiByteToWideChar(CP_UTF8) predates strict UTF-8 and
 * is known to accept overlongs and surrogates; when the differential against
 * XP runs, a divergence flag goes here and the strictness becomes an option.
 *
 * No allocation, no global state beyond const tables. XP's kernel32
 * MultiByteToWideChar/CharToByte is the eventual target ABI — not a contract
 * yet (kernel32 is not rebuildable in P1).
 */
#ifndef ANEMOS_NLS_H
#define ANEMOS_NLS_H

#include <stddef.h>
#include <stdint.h>

#include "../include/anem_status.h"

/* One byte <-> one UTF-16 code unit. ANEM_FORMAT means "undefined in this
 * codepage" (forward) or "not representable in CP1252" (reverse). */
AnemStatus anem_cp1252_to_uni(uint8_t byte, uint16_t *uni);
AnemStatus anem_uni_to_cp1252(uint16_t uni, uint8_t *byte);

/* One Unicode code point <-> 1..4 UTF-8 bytes. ANEM_FORMAT means the code
 * point is not encodable (surrogate, > U+10FFFF) or the bytes are not a
 * valid UTF-8 sequence (overlong, truncated, stray continuation, ...). */
AnemStatus anem_utf8_encode(uint32_t codepoint, uint8_t out[4], size_t *written);
AnemStatus anem_utf8_decode(const uint8_t *text, size_t len,
                            uint32_t *codepoint, size_t *consumed);

#endif /* ANEMOS_NLS_H */
