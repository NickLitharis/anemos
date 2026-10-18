/* Vector suite for anemos/crc32.
 *
 * The first two cases are the widely-published CRC-32 values ("a" ->
 * 0xE8B7BE43, "abc" -> 0x352441C2); every expected value was generated with
 * Python's zlib — an independent implementation — by vectors.py.
 *
 * The 55/56 and 63/64/65 lengths are the padding-independent boundaries a
 * streaming CRC user hits; the LCG cases pin real binary data.
 *
 * Each vector is hashed three ways (one shot, uneven chunks through
 * anem_crc32_update, unaligned pointer), because "correct for one call
 * pattern" is not correct.
 */
#include <stdio.h>
#include <string.h>

#include "crc32.h"

enum pattern {
    PAT_EMPTY, PAT_TEXT, PAT_X, PAT_DIGITS, PAT_SEQ, PAT_FF, PAT_ZERO, PAT_LCG
};

struct vector {
    const char *name;
    size_t len;
    uint32_t crc;
    enum pattern pattern;
    const char *text;       /* PAT_TEXT only; strlen(text) == len */
};

static const struct vector k_vectors[] = {
    { "empty",         0, 0x00000000u, PAT_EMPTY, NULL },
    { "a",             1, 0xe8b7be43u, PAT_TEXT,  "a" },
    { "abc",           3, 0x352441c2u, PAT_TEXT,  "abc" },
    { "message digest", 14, 0x20159d7fu, PAT_TEXT, "message digest" },
    { "alpha",        26, 0x4c2750bdu, PAT_TEXT,
      "abcdefghijklmnopqrstuvwxyz" },
    { "alnum",        62, 0x1fc2e6d2u, PAT_TEXT,
      "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789" },
    { "digits10x8",   80, 0x7ca94a72u, PAT_DIGITS, NULL },
    { "55_bytes",     55, 0xce74275cu, PAT_X,      NULL },
    { "56_bytes",     56, 0xeecf7f7bu, PAT_X,      NULL },
    { "63_bytes",     63, 0x6897ac3fu, PAT_X,      NULL },
    { "64_bytes",     64, 0x3ad2ac12u, PAT_X,      NULL },
    { "65_bytes",     65, 0x7f5fb567u, PAT_X,      NULL },
    { "255_bytes",    255, 0xd32f9ba0u, PAT_SEQ,   NULL },
    { "binary_ff",    100, 0x03d28681u, PAT_FF,    NULL },
    { "nul_bytes",    64, 0x758d6336u, PAT_ZERO,  NULL },
    { "lcg_64",       64, 0x3c04b8abu, PAT_LCG,   NULL },
    { "lcg_255",      255, 0xf5c338acu, PAT_LCG,  NULL },
    { "lcg_1000",     1000, 0x1f52fd1cu, PAT_LCG, NULL },
};

/* Golden messages: an LCG stream, x = (1103515245*x + 12345) mod 2^31,
 * byte = (x >> 16) & 0xFF, seeded x = 1 — reproducible in Python
 * (vectors.py) and in C alike, so no 1000-byte literal is needed. */
static void lcg_fill(uint8_t *buf, size_t n)
{
    uint32_t x = 1;
    for (size_t i = 0; i < n; i++) {
        x = x * 1103515245u + 12345u;
        x &= 0x7fffffffu;
        buf[i] = (uint8_t)(x >> 16);
    }
}

static void make_msg(const struct vector *vec, uint8_t *buf)
{
    switch (vec->pattern) {
    case PAT_EMPTY:
        break;
    case PAT_TEXT:
        memcpy(buf, vec->text, vec->len);
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
    case PAT_LCG:
        lcg_fill(buf, vec->len);
        break;
    }
}

/* One shot: anem_crc32 over the whole message. */
static bool check_one_shot(const struct vector *vec, const uint8_t *msg)
{
    uint32_t crc = 0xdeadbeefu;

    if (anem_crc32(msg, vec->len, &crc) != ANEM_OK) {
        printf("FAIL %s one-shot: status\n", vec->name);
        return false;
    }
    if (crc != vec->crc) {
        printf("FAIL %s one-shot: got 0x%08x want 0x%08x\n",
               vec->name, crc, vec->crc);
        return false;
    }
    return true;
}

/* Uneven chunk boundaries through anem_crc32_update, cycling 1..17 bytes. */
static bool check_chunked(const struct vector *vec, const uint8_t *msg)
{
    uint32_t running = 0;
    size_t offset = 0;

    if (anem_crc32_update(0, NULL, 0, &running) != ANEM_OK || running != 0) {
        printf("FAIL %s chunked: seed\n", vec->name);
        return false;
    }
    while (offset < vec->len) {
        size_t step = (offset % 17) + 1;
        if (step > vec->len - offset) {
            step = vec->len - offset;
        }
        if (anem_crc32_update(running, msg + offset, step, &running) != ANEM_OK) {
            printf("FAIL %s chunked: update\n", vec->name);
            return false;
        }
        offset += step;
    }
    if (running != vec->crc) {
        printf("FAIL %s chunked: got 0x%08x want 0x%08x\n",
               vec->name, running, vec->crc);
        return false;
    }
    /* continuing with nothing must change nothing */
    if (anem_crc32_update(running, NULL, 0, &running) != ANEM_OK
        || running != vec->crc) {
        printf("FAIL %s chunked: empty continuation\n", vec->name);
        return false;
    }
    return true;
}

/* Deliberately unaligned buffer. */
static bool check_unaligned(const struct vector *vec, const uint8_t *msg)
{
    static uint8_t shifted[1001];
    uint32_t crc = 0;

    memcpy(shifted + 1, msg, vec->len);
    if (anem_crc32(shifted + 1, vec->len, &crc) != ANEM_OK) {
        printf("FAIL %s unaligned: status\n", vec->name);
        return false;
    }
    if (crc != vec->crc) {
        printf("FAIL %s unaligned: got 0x%08x want 0x%08x\n",
               vec->name, crc, vec->crc);
        return false;
    }
    return true;
}

static bool check_null_contract(void)
{
    static const uint8_t msg[3] = { 1, 2, 3 };
    uint32_t crc = 0;
    bool ok = true;

    ok = (anem_crc32(NULL, 1, &crc) == ANEM_BADARG) && ok;
    ok = (anem_crc32(msg, 3, NULL) == ANEM_BADARG) && ok;
    ok = (anem_crc32(NULL, 0, &crc) == ANEM_OK) && (crc == 0) && ok;
    ok = (anem_crc32_update(1234, NULL, 1, &crc) == ANEM_BADARG) && ok;
    ok = (anem_crc32_update(1234, msg, 3, NULL) == ANEM_BADARG) && ok;

    printf("%s null-argument contract\n", ok ? "PASS" : "FAIL");
    return ok;
}

int main(void)
{
    static uint8_t msg[1000];
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
