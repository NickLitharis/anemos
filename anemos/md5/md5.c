/* MD5 — RFC 1321. See md5.h for why this exists. */
#include "md5.h"

/* RFC 1321 §3.4: the rotate-left amount of each of the 64 steps, and
 * K[i] = floor(2^32 * abs(sin(i + 1))) — the constants that make MD5
 * reproducible across implementations. */
static const uint8_t k_shift[64] = {
     7, 12, 17, 22,  7, 12, 17, 22,  7, 12, 17, 22,  7, 12, 17, 22,
     5,  9, 14, 20,  5,  9, 14, 20,  5,  9, 14, 20,  5,  9, 14, 20,
     4, 11, 16, 23,  4, 11, 16, 23,  4, 11, 16, 23,  4, 11, 16, 23,
     6, 10, 15, 21,  6, 10, 15, 21,  6, 10, 15, 21,  6, 10, 15, 21,
};

static const uint32_t k_sin[64] = {
    0xd76aa478u, 0xe8c7b756u, 0x242070dbu, 0xc1bdceeeu,
    0xf57c0fafu, 0x4787c62au, 0xa8304613u, 0xfd469501u,
    0x698098d8u, 0x8b44f7afu, 0xffff5bb1u, 0x895cd7beu,
    0x6b901122u, 0xfd987193u, 0xa679438eu, 0x49b40821u,
    0xf61e2562u, 0xc040b340u, 0x265e5a51u, 0xe9b6c7aau,
    0xd62f105du, 0x02441453u, 0xd8a1e681u, 0xe7d3fbc8u,
    0x21e1cde6u, 0xc33707d6u, 0xf4d50d87u, 0x455a14edu,
    0xa9e3e905u, 0xfcefa3f8u, 0x676f02d9u, 0x8d2a4c8au,
    0xfffa3942u, 0x8771f681u, 0x6d9d6122u, 0xfde5380cu,
    0xa4beea44u, 0x4bdecfa9u, 0xf6bb4b60u, 0xbebfbc70u,
    0x289b7ec6u, 0xeaa127fau, 0xd4ef3085u, 0x04881d05u,
    0xd9d4d039u, 0xe6db99e5u, 0x1fa27cf8u, 0xc4ac5665u,
    0xf4292244u, 0x432aff97u, 0xab9423a7u, 0xfc93a039u,
    0x655b59c3u, 0x8f0ccc92u, 0xffeff47du, 0x85845dd1u,
    0x6fa87e4fu, 0xfe2ce6e0u, 0xa3014314u, 0x4e0811a1u,
    0xf7537e82u, 0xbd3af235u, 0x2ad7d2bbu, 0xeb86d391u,
};

static uint32_t rotl32(uint32_t x, uint8_t n)
{
    return (uint32_t)((x << n) | (x >> (32u - n)));
}

/* Little-endian word load. Byte-at-a-time rather than a cast, because an
 * unaligned or differently-endian cast is undefined behaviour, and callers
 * hand us buffers from anywhere (OS-CODING-RULES §7). */
static void block_to_words(const uint8_t block[64], uint32_t words[16])
{
    for (unsigned i = 0; i < 16; i++) {
        words[i] = (uint32_t)block[i * 4]
                 | ((uint32_t)block[i * 4 + 1] << 8)
                 | ((uint32_t)block[i * 4 + 2] << 16)
                 | ((uint32_t)block[i * 4 + 3] << 24);
    }
}

/* One block, per RFC 1321 §3.4. */
static AnemStatus transform(AnemMd5 *ctx, const uint8_t block[64])
{
    uint32_t m[16];
    uint32_t a, b, c, d;

    if (ctx == NULL || block == NULL) {
        return ANEM_BADARG;
    }
    if (ctx->used != 64) {          /* invariant: a complete block is waiting */
        return ANEM_BADARG;
    }

    block_to_words(block, m);
    a = ctx->state[0];
    b = ctx->state[1];
    c = ctx->state[2];
    d = ctx->state[3];

    for (unsigned i = 0; i < 64; i++) {
        uint32_t f;
        uint32_t g;
        if (i < 16) {
            f = (b & c) | (~b & d);
            g = i;
        } else if (i < 32) {
            f = (d & b) | (~d & c);
            g = (5u * i + 1u) % 16u;
        } else if (i < 48) {
            f = b ^ c ^ d;
            g = (3u * i + 5u) % 16u;
        } else {
            f = c ^ (b | ~d);
            g = (7u * i) % 16u;
        }
        f = f + a + k_sin[i] + m[g];
        a = d;
        d = c;
        c = b;
        b = b + rotl32(f, k_shift[i]);
    }

    ctx->state[0] += a;
    ctx->state[1] += b;
    ctx->state[2] += c;
    ctx->state[3] += d;

#ifdef ANEM_MD5_NEGATIVE
    /* Negative control, enabled only by run.sh's second build: corrupt the
     * state so the suite is proven able to fail. A check that cannot fail is
     * not a check. */
    ctx->state[0] ^= 1u;
#endif
    return ANEM_OK;
}

AnemStatus anem_md5_init(AnemMd5 *ctx)
{
    if (ctx == NULL) {
        return ANEM_BADARG;
    }
    ctx->state[0] = 0x67452301u;
    ctx->state[1] = 0xefcdab89u;
    ctx->state[2] = 0x98badcfeu;
    ctx->state[3] = 0x10325476u;
    ctx->bits = 0;
    ctx->used = 0;
    for (unsigned i = 0; i < 64; i++) {   /* no uninitialised bytes anywhere */
        ctx->block[i] = 0;
    }
    return ANEM_OK;
}

AnemStatus anem_md5_update(AnemMd5 *ctx, const void *data, size_t len)
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

    /* MD5's length field is 64 bits by definition (RFC 1321 §3.2), so this
     * counter wrapping is specified behaviour, not an overflow to defend. */
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

AnemStatus anem_md5_final(AnemMd5 *ctx, uint8_t digest[16])
{
    uint64_t bits;
    uint8_t pad = 0x80u;
    uint8_t zero = 0u;
    uint8_t length_le[8];

    if (ctx == NULL || digest == NULL) {
        return ANEM_BADARG;
    }

    bits = ctx->bits;               /* captured before padding moves it */

    if (anem_md5_update(ctx, &pad, 1) != ANEM_OK) {
        return ANEM_BADARG;
    }
    while (ctx->used != 56) {       /* terminates: used rises by one each pass */
        if (anem_md5_update(ctx, &zero, 1) != ANEM_OK) {
            return ANEM_BADARG;
        }
    }

    for (unsigned i = 0; i < 8; i++) {
        length_le[i] = (uint8_t)(bits >> (8u * i));
    }
    if (anem_md5_update(ctx, length_le, sizeof length_le) != ANEM_OK) {
        return ANEM_BADARG;
    }
    if (ctx->used != 0) {           /* the length must have completed a block */
        return ANEM_BADARG;
    }

    for (unsigned i = 0; i < 4; i++) {
        digest[i * 4]     = (uint8_t)(ctx->state[i]);
        digest[i * 4 + 1] = (uint8_t)(ctx->state[i] >> 8);
        digest[i * 4 + 2] = (uint8_t)(ctx->state[i] >> 16);
        digest[i * 4 + 3] = (uint8_t)(ctx->state[i] >> 24);
    }
    return ANEM_OK;
}
