/* SHA-1 — FIPS 180-1. See sha1.h for why this exists. */
#include "sha1.h"

/* FIPS 180-1 §7: the five round constants, K[0..3]. */
static const uint32_t k_round[4] = {
    0x5a827999u, 0x6ed9eba1u, 0x8f1bbcdcu, 0xca62c1d6u,
};

static uint32_t rotl32(uint32_t x, unsigned n)
{
    return (uint32_t)((x << n) | (x >> (32u - n)));
}

/* Big-endian word load. Byte-at-a-time rather than a cast, because an
 * unaligned or differently-endian cast is undefined behaviour, and callers
 * hand us buffers from anywhere (OS-CODING-RULES §7). */
static void block_to_words(const uint8_t block[64], uint32_t words[16])
{
    for (unsigned i = 0; i < 16; i++) {
        words[i] = ((uint32_t)block[i * 4] << 24)
                 | ((uint32_t)block[i * 4 + 1] << 16)
                 | ((uint32_t)block[i * 4 + 2] << 8)
                 | (uint32_t)block[i * 4 + 3];
    }
}

/* One block, per FIPS 180-1 §7. */
static AnemStatus transform(AnemSha1 *ctx, const uint8_t block[64])
{
    uint32_t w[80];
    uint32_t a, b, c, d, e;

    if (ctx == NULL || block == NULL) {
        return ANEM_BADARG;
    }
    if (ctx->used != 64) {          /* invariant: a complete block is waiting */
        return ANEM_BADARG;
    }

    block_to_words(block, w);
    for (unsigned i = 16; i < 80; i++) {
        w[i] = rotl32(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
    }

    a = ctx->state[0];
    b = ctx->state[1];
    c = ctx->state[2];
    d = ctx->state[3];
    e = ctx->state[4];

    for (unsigned i = 0; i < 80; i++) {
        uint32_t f;
        uint32_t k;
        if (i < 20) {
            f = (b & c) | (~b & d);
        } else if (i < 40) {
            f = b ^ c ^ d;
        } else if (i < 60) {
            f = (b & c) | (b & d) | (c & d);
        } else {
            f = b ^ c ^ d;
        }
        k = k_round[i / 20];
        /* temp must be computed before the register shift: e is about to
         * become d, and the round consumes the old e. */
        {
            uint32_t temp = rotl32(a, 5) + f + e + k + w[i];
            e = d;
            d = c;
            c = rotl32(b, 30);
            b = a;
            a = temp;
        }
    }

    ctx->state[0] += a;
    ctx->state[1] += b;
    ctx->state[2] += c;
    ctx->state[3] += d;
    ctx->state[4] += e;

#ifdef ANEM_SHA1_NEGATIVE
    /* Negative control, enabled only by run.sh's second build: corrupt the
     * state so the suite is proven able to fail. A check that cannot fail is
     * not a check. */
    ctx->state[0] ^= 1u;
#endif
    return ANEM_OK;
}

AnemStatus anem_sha1_init(AnemSha1 *ctx)
{
    if (ctx == NULL) {
        return ANEM_BADARG;
    }
    ctx->state[0] = 0x67452301u;
    ctx->state[1] = 0xefcdab89u;
    ctx->state[2] = 0x98badcfeu;
    ctx->state[3] = 0x10325476u;
    ctx->state[4] = 0xc3d2e1f0u;
    ctx->bits = 0;
    ctx->used = 0;
    for (unsigned i = 0; i < 64; i++) {   /* no uninitialised bytes anywhere */
        ctx->block[i] = 0;
    }
    return ANEM_OK;
}

AnemStatus anem_sha1_update(AnemSha1 *ctx, const void *data, size_t len)
{
    const uint8_t *p = data;

    if (ctx == NULL) {
        return ANEM_BADARG;
    }
    if (len != 0 && data == NULL) {
        return ANEM_BADARG;
    }
    if (ctx->used >= 64) {          /* invariant: never true between calls */
        return ANEM_BADARG;
    }

    /* SHA-1's length field is 64 bits by definition (FIPS 180-1 §5.1.1), so
     * this counter wrapping is specified behaviour, not an overflow to defend. */
    ctx->bits += (uint64_t)len * 8u;

    while (len > 0) {               /* terminates: len drops by take >= 1 */
        uint32_t room = 64u - ctx->used;
        size_t take = (len < (size_t)room) ? len : (size_t)room;
        for (size_t i = 0; i < take; i++) {
            ctx->block[ctx->used + (uint32_t)i] = p[i];
        }
        ctx->used += (uint32_t)take;
        p += take;
        len -= take;
        if (ctx->used == 64) {
            AnemStatus st = transform(ctx, ctx->block);
            if (st != ANEM_OK) {
                return st;
            }
            ctx->used = 0;
        }
    }
    return ANEM_OK;
}

AnemStatus anem_sha1_final(AnemSha1 *ctx, uint8_t digest[20])
{
    uint64_t bits;
    uint8_t pad = 0x80u;
    uint8_t zero = 0u;
    uint8_t length_be[8];

    if (ctx == NULL || digest == NULL) {
        return ANEM_BADARG;
    }

    bits = ctx->bits;               /* captured before padding moves it */

    if (anem_sha1_update(ctx, &pad, 1) != ANEM_OK) {
        return ANEM_BADARG;
    }
    while (ctx->used != 56) {       /* terminates: used rises by one each pass */
        if (anem_sha1_update(ctx, &zero, 1) != ANEM_OK) {
            return ANEM_BADARG;
        }
    }

    for (unsigned i = 0; i < 8; i++) {
        length_be[i] = (uint8_t)(bits >> (56u - 8u * i));
    }
    if (anem_sha1_update(ctx, length_be, sizeof length_be) != ANEM_OK) {
        return ANEM_BADARG;
    }
    if (ctx->used != 0) {           /* the length must have completed a block */
        return ANEM_BADARG;
    }

    for (unsigned i = 0; i < 5; i++) {
        digest[i * 4]     = (uint8_t)(ctx->state[i] >> 24);
        digest[i * 4 + 1] = (uint8_t)(ctx->state[i] >> 16);
        digest[i * 4 + 2] = (uint8_t)(ctx->state[i] >> 8);
        digest[i * 4 + 3] = (uint8_t)(ctx->state[i]);
    }
    return ANEM_OK;
}
