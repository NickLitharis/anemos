/* Vector suite for anemos/md5.
 *
 * The first seven cases are RFC 1321's own published suite (§A.5). The rest
 * are boundary lengths where MD5's padding logic changes shape: 55/56 bytes
 * is where the length field stops fitting in the final block, 63/64/65 is the
 * block boundary, and one case exceeds 32 bits of message length.
 *
 * Expected digests were generated with Python's hashlib — an independent
 * implementation — by vectors.py; re-run it to re-derive every number here.
 *
 * Each vector is hashed three ways (one shot, uneven chunks, unaligned
 * pointer), because "correct for one call pattern" is not correct.
 */
#include <stdio.h>
#include <string.h>

#include "md5.h"

enum pattern {
    PAT_EMPTY, PAT_TEXT, PAT_A, PAT_X, PAT_DIGITS, PAT_SEQ, PAT_FF, PAT_ZERO
};

struct vector {
    const char *name;
    size_t len;
    const char *hex;
    enum pattern pattern;
    const char *text;       /* PAT_TEXT only; strlen(text) == len */
};

static const struct vector k_vectors[] = {
    { "empty",          0,       "d41d8cd98f00b204e9800998ecf8427e", PAT_EMPTY, NULL },
    { "a",              1,       "0cc175b9c0f1b6a831c399e269772661", PAT_TEXT,  "a" },
    { "abc",            3,       "900150983cd24fb0d6963f7d28e17f72", PAT_TEXT,  "abc" },
    { "message digest", 14,      "f96b697d7cb7938d525a2f31aaf161d0", PAT_TEXT,  "message digest" },
    { "alpha",          26,      "c3fcd3d76192e4007dfb496cca67e13b", PAT_TEXT,
      "abcdefghijklmnopqrstuvwxyz" },
    { "alnum",          62,      "d174ab98d277d9f5a5611c2c9f419d9f", PAT_TEXT,
      "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789" },
    { "digits10x8",     80,      "57edf4a22be3c955ac49da2e2107b67a", PAT_DIGITS, NULL },
    { "million_a",      1000000, "7707d6ae4e027c70eea2a935c2296f21", PAT_A,      NULL },
    { "55_bytes",       55,      "04364420e25c512fd958a70738aa8f72", PAT_X,      NULL },
    { "56_bytes",       56,      "668a72d5ba17f08e62dabcafad6db14b", PAT_X,      NULL },
    { "63_bytes",       63,      "7dc2ca208106a2f703567bdff99d8981", PAT_X,      NULL },
    { "64_bytes",       64,      "c1bb4f81d892b2d57947682aeb252456", PAT_X,      NULL },
    { "65_bytes",       65,      "1bc932052302d074bdec39795fe00cf6", PAT_X,      NULL },
    { "255_bytes",      255,     "11b7aaa64c413d2f0fccf893881c46a2", PAT_SEQ,    NULL },
    { "binary_ff",      100,     "3440bde6a534b0d5e7d76f4a5ea4cb27", PAT_FF,     NULL },
    { "nul_bytes",      64,      "3b5d3c7d207e37dceeedd301e35e2e58", PAT_ZERO,   NULL },
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
    case PAT_DIGITS:
        for (size_t i = 0; i < vec->len; i++) {
            buf[i] = (uint8_t)("1234567890"[i % 10]);
        }
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

static void to_hex(const uint8_t digest[16], char out[33])
{
    static const char digits[] = "0123456789abcdef";
    for (unsigned i = 0; i < 16; i++) {
        out[i * 2]     = digits[digest[i] >> 4];
        out[i * 2 + 1] = digits[digest[i] & 0x0fu];
    }
    out[32] = '\0';
}

/* Finalise an already-fed context and compare. Returns false and prints on
 * the first mismatch; `label` says which call pattern failed. */
static bool final_matches(const struct vector *vec, const char *label, AnemMd5 *ctx)
{
    uint8_t digest[16];
    char hex[33];

    if (anem_md5_final(ctx, digest) != ANEM_OK) {
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
    AnemMd5 ctx;

    if (anem_md5_init(&ctx) != ANEM_OK) {
        printf("FAIL %s one-shot: init\n", vec->name);
        return false;
    }
    if (anem_md5_update(&ctx, msg, vec->len) != ANEM_OK) {
        printf("FAIL %s one-shot: update\n", vec->name);
        return false;
    }
    return final_matches(vec, "one-shot", &ctx);
}

/* Uneven chunk boundaries, cycling 1..17 bytes. */
static bool check_chunked(const struct vector *vec, const uint8_t *msg)
{
    AnemMd5 ctx;
    size_t offset = 0;

    if (anem_md5_init(&ctx) != ANEM_OK) {
        printf("FAIL %s chunked: init\n", vec->name);
        return false;
    }
    while (offset < vec->len) {
        size_t step = (offset % 17) + 1;
        if (step > vec->len - offset) {
            step = vec->len - offset;
        }
        if (anem_md5_update(&ctx, msg + offset, step) != ANEM_OK) {
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
    AnemMd5 ctx;

    memcpy(shifted + 1, msg, vec->len);
    if (anem_md5_init(&ctx) != ANEM_OK) {
        printf("FAIL %s unaligned: init\n", vec->name);
        return false;
    }
    if (anem_md5_update(&ctx, shifted + 1, vec->len) != ANEM_OK) {
        printf("FAIL %s unaligned: update\n", vec->name);
        return false;
    }
    return final_matches(vec, "unaligned", &ctx);
}

static bool check_null_contract(void)
{
    AnemMd5 ctx;
    uint8_t digest[16];
    bool ok = true;

    ok = (anem_md5_init(NULL) == ANEM_BADARG) && ok;
    ok = (anem_md5_init(&ctx) == ANEM_OK) && ok;
    ok = (anem_md5_update(NULL, digest, 1) == ANEM_BADARG) && ok;
    ok = (anem_md5_update(&ctx, NULL, 5) == ANEM_BADARG) && ok;
    ok = (anem_md5_update(&ctx, NULL, 0) == ANEM_OK) && ok;
    ok = (anem_md5_final(&ctx, NULL) == ANEM_BADARG) && ok;
    ok = (anem_md5_final(NULL, digest) == ANEM_BADARG) && ok;

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
