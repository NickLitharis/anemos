/* Vector suite for anemos/sha1.
 *
 * The first four cases are FIPS 180-1's own published suite (§7.3-7.4): the
 * empty message, "abc", the 56-byte two-block message, and one million 'a'.
 * The rest are boundary lengths where SHA-1's padding logic changes shape:
 * 55/56 bytes is where the length field stops fitting in the final block,
 * 63/64/65 is the block boundary, and one case exceeds 32 bits of length.
 *
 * Expected digests were generated with Python's hashlib — an independent
 * implementation — by vectors.py; re-run it to re-derive every number here.
 *
 * Each vector is hashed three ways (one shot, uneven chunks, unaligned
 * pointer), because "correct for one call pattern" is not correct.
 */
#include <stdio.h>
#include <string.h>

#include "sha1.h"

enum pattern {
    PAT_EMPTY, PAT_TEXT, PAT_A, PAT_X, PAT_SEQ, PAT_FF, PAT_ZERO
};

struct vector {
    const char *name;
    size_t len;
    const char *hex;
    enum pattern pattern;
    const char *text;       /* PAT_TEXT only; strlen(text) == len */
};

static const struct vector k_vectors[] = {
    { "empty",     0,       "da39a3ee5e6b4b0d3255bfef95601890afd80709", PAT_EMPTY, NULL },
    { "abc",       3,       "a9993e364706816aba3e25717850c26c9cd0d89d", PAT_TEXT,  "abc" },
    { "two_block", 56,      "84983e441c3bd26ebaae4aa1f95129e5e54670f1", PAT_TEXT,
      "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq" },
    { "million_a", 1000000, "34aa973cd4c4daa4f61eeb2bdbad27316534016f", PAT_A,  NULL },
    { "55_bytes",  55,      "cef734ba81a024479e09eb5a75b6ddae62e6abf1", PAT_X,  NULL },
    { "56_bytes",  56,      "901305367c259952f4e7af8323f480d59f81335b", PAT_X,  NULL },
    { "63_bytes",  63,      "0ddc4e0cccd9a12850deb5abb0853a4425559fec", PAT_X,  NULL },
    { "64_bytes",  64,      "bb2fa3ee7afb9f54c6dfb5d021f14b1ffe40c163", PAT_X,  NULL },
    { "65_bytes",  65,      "78c741ddc482e4cdf8c474a0876347a0905b6233", PAT_X,  NULL },
    { "255_bytes", 255,     "fa2c27c443e60a0bcd8a1eb82d20fec20759c03e", PAT_SEQ, NULL },
    { "binary_ff", 100,     "b0c4cf3628c87f77cb52f1727d22ea7fd671c092", PAT_FF,  NULL },
    { "nul_bytes", 64,      "c8d7d0ef0eedfa82d2ea1aa592845b9a6d4b02b7", PAT_ZERO, NULL },
    { "alnum",     62,      "761c457bf73b14d27e9e9265c46f4b4dda11f940", PAT_TEXT,
      "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789" },
};

static void make_msg(const struct vector *vec, uint8_t *buf)
{
    switch (vec->pattern) {
    case PAT_EMPTY:
        break;
    case PAT_TEXT:
        memcpy(buf, vec->text, vec->len);
        break;
    case PAT_A:
        memset(buf, 'a', vec->len);
        break;
    case PAT_X:
        memset(buf, 'x', vec->len);
        break;
    case PAT_SEQ:
        for (size_t i = 0; i < vec->len; i++) {
            buf[i] = (uint8_t)i;
        }
        break;
    case PAT_FF:
        memset(buf, 0xff, vec->len);
        break;
    case PAT_ZERO:
        memset(buf, 0, vec->len);
        break;
    }
}

static void to_hex(const uint8_t digest[20], char out[41])
{
    static const char digits[] = "0123456789abcdef";
    for (unsigned i = 0; i < 20; i++) {
        out[i * 2]     = digits[digest[i] >> 4];
        out[i * 2 + 1] = digits[digest[i] & 0x0fu];
    }
    out[40] = '\0';
}

/* Finalise an already-fed context and compare. Returns false and prints on
 * the first mismatch; `label` says which call pattern failed. */
static bool final_matches(const struct vector *vec, const char *label, AnemSha1 *ctx)
{
    uint8_t digest[20];
    char hex[41];

    if (anem_sha1_final(ctx, digest) != ANEM_OK) {
        printf("FAIL %s %s: final\n", vec->name, label);
        return false;
    }
    to_hex(digest, hex);
    if (strcmp(hex, vec->hex) != 0) {
        printf("FAIL %s %s: got %s want %s\n", vec->name, label, hex, vec->hex);
        return false;
    }
    return true;
}

/* One shot: init, one update, final. */
static bool check_one_shot(const struct vector *vec, const uint8_t *msg)
{
    AnemSha1 ctx;

    if (anem_sha1_init(&ctx) != ANEM_OK) {
        printf("FAIL %s one-shot: init\n", vec->name);
        return false;
    }
    if (anem_sha1_update(&ctx, msg, vec->len) != ANEM_OK) {
        printf("FAIL %s one-shot: update\n", vec->name);
        return false;
    }
    return final_matches(vec, "one-shot", &ctx);
}

/* Uneven chunk boundaries, cycling 1..17 bytes. */
static bool check_chunked(const struct vector *vec, const uint8_t *msg)
{
    AnemSha1 ctx;
    size_t offset = 0;

    if (anem_sha1_init(&ctx) != ANEM_OK) {
        printf("FAIL %s chunked: init\n", vec->name);
        return false;
    }
    while (offset < vec->len) {
        size_t step = (offset % 17) + 1;
        if (step > vec->len - offset) {
            step = vec->len - offset;
        }
        if (anem_sha1_update(&ctx, msg + offset, step) != ANEM_OK) {
            printf("FAIL %s chunked: update\n", vec->name);
            return false;
        }
        offset += step;
    }
    return final_matches(vec, "chunked", &ctx);
}

/* Deliberately unaligned buffer. */
static bool check_unaligned(const struct vector *vec, const uint8_t *msg)
{
    static uint8_t shifted[1000001];
    AnemSha1 ctx;

    memcpy(shifted + 1, msg, vec->len);
    if (anem_sha1_init(&ctx) != ANEM_OK) {
        printf("FAIL %s unaligned: init\n", vec->name);
        return false;
    }
    if (anem_sha1_update(&ctx, shifted + 1, vec->len) != ANEM_OK) {
        printf("FAIL %s unaligned: update\n", vec->name);
        return false;
    }
    return final_matches(vec, "unaligned", &ctx);
}

static bool check_null_contract(void)
{
    AnemSha1 ctx;
    uint8_t digest[20];
    bool ok = true;

    ok = (anem_sha1_init(NULL) == ANEM_BADARG) && ok;
    ok = (anem_sha1_init(&ctx) == ANEM_OK) && ok;
    ok = (anem_sha1_update(NULL, digest, 1) == ANEM_BADARG) && ok;
    ok = (anem_sha1_update(&ctx, NULL, 5) == ANEM_BADARG) && ok;
    ok = (anem_sha1_update(&ctx, NULL, 0) == ANEM_OK) && ok;
    ok = (anem_sha1_final(&ctx, NULL) == ANEM_BADARG) && ok;
    ok = (anem_sha1_final(NULL, digest) == ANEM_BADARG) && ok;

    printf("%s null-argument contract\n", ok ? "PASS" : "FAIL");
    return ok;
}

int main(void)
{
    static uint8_t msg[1000000];
    unsigned failures = 0;

    for (size_t v = 0; v < sizeof k_vectors / sizeof k_vectors[0]; v++) {
        const struct vector *vec = &k_vectors[v];
        if (vec->len > sizeof msg) {
            printf("FAIL %s: message buffer too small\n", vec->name);
            failures++;
            continue;
        }
        make_msg(vec, msg);
        /* each check runs even if an earlier one failed — all three matter */
        bool ok = check_one_shot(vec, msg);
        ok = check_chunked(vec, msg) && ok;
        ok = check_unaligned(vec, msg) && ok;
        if (ok) {
            printf("PASS %s (%zu bytes)\n", vec->name, vec->len);
        } else {
            failures++;
        }
    }
    if (!check_null_contract()) {
        failures++;
    }

    if (failures != 0) {
        printf("FAILED: %u vector(s)\n", failures);
        return 1;
    }
    printf("all vectors pass\n");
    return 0;
}
