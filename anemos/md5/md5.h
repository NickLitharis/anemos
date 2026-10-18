/* MD5 — RFC 1321.
 *
 * XP's ntdll exports MD5Init/MD5Update/MD5Final; this is Anemos's own
 * implementation of the same *algorithm*, not yet a drop-in for that ABI
 * (ntdll is not rebuildable in P1 — see PLAN.md). XP remains the oracle for
 * output values: the expected digests in test_md5.c were generated with an
 * independent implementation, and the first seven match RFC 1321's own
 * published suite (§A.5).
 *
 * MD5 is cryptographically broken. It exists because XP's binaries use it and
 * compatibility with XP is the goal — never use it for signatures.
 *
 * All entry points allocate nothing, keep no global state and touch no
 * caller memory beyond their arguments, so callers may hash concurrently in
 * separate contexts without a lock. Any function that can fail says so with
 * an AnemStatus (OS-CODING-RULES §4, §11).
 */
#ifndef ANEMOS_MD5_H
#define ANEMOS_MD5_H

#include <stddef.h>
#include <stdint.h>

typedef enum AnemStatus {
    ANEM_OK = 0,
    ANEM_BADARG,        /* NULL pointer, or an argument that breaks an invariant */
} AnemStatus;

typedef struct AnemMd5 {
    uint32_t state[4];  /* A, B, C, D (RFC 1321 §3.3) */
    uint64_t bits;      /* message length in bits, mod 2^64 by definition */
    uint8_t  block[64]; /* bytes of the current partial block */
    uint32_t used;      /* how many bytes of block[] are occupied; always < 64
                         * between calls, and == 64 only while a full block is
                         * being transformed */
} AnemMd5;

AnemStatus anem_md5_init(AnemMd5 *ctx);
AnemStatus anem_md5_update(AnemMd5 *ctx, const void *data, size_t len);
AnemStatus anem_md5_final(AnemMd5 *ctx, uint8_t digest[16]);

#endif /* ANEMOS_MD5_H */
