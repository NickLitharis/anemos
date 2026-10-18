/* SHA-1 — FIPS 180-1.
 *
 * XP's ntdll exports A_SHAInit/A_SHAUpdate/A_SHAFinal; this is Anemos's own
 * implementation of the same *algorithm*, not yet a drop-in for that ABI
 * (ntdll is not rebuildable in P1 — see PLAN.md). XP remains the oracle for
 * output values: the expected digests in test_sha1.c were generated with an
 * independent implementation, and the first four match FIPS 180-1's own
 * published suite.
 *
 * SHA-1 is broken for signatures (SHAttered, 2017). It exists because XP's
 * binaries use it and compatibility with XP is the goal.
 *
 * All entry points allocate nothing, keep no global state and touch no
 * caller memory beyond their arguments, so callers may hash concurrently in
 * separate contexts without a lock. Any function that can fail says so with
 * an AnemStatus (OS-CODING-RULES §4, §11).
 */
#ifndef ANEMOS_SHA1_H
#define ANEMOS_SHA1_H

#include <stddef.h>
#include <stdint.h>

typedef enum AnemStatus {
    ANEM_OK = 0,
    ANEM_BADARG,        /* NULL pointer, or an argument that breaks an invariant */
} AnemStatus;

typedef struct AnemSha1 {
    uint32_t state[5];  /* H0..H4 (FIPS 180-1 §6) */
    uint64_t bits;      /* message length in bits, mod 2^64 */
    uint8_t  block[64]; /* bytes of the current partial block */
    uint32_t used;      /* bytes of block[] occupied; always < 64 between
                         * calls, and == 64 only while transforming */
} AnemSha1;

AnemStatus anem_sha1_init(AnemSha1 *ctx);
AnemStatus anem_sha1_update(AnemSha1 *ctx, const void *data, size_t len);
AnemStatus anem_sha1_final(AnemSha1 *ctx, uint8_t digest[20]);

#endif /* ANEMOS_SHA1_H */
