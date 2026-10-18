/* Vector suite for anemos/nls.
 *
 * CP1252 is checked against expectations the test states itself — the 27
 * defined bytes in 0x80..0x9f are hardcoded as Unicode constants and the
 * identity ranges are checked against the formula — so the 256-entry table
 * is never its own oracle. UTF-8 is checked against RFC 3629's rules: every
 * encodable code point round-trips, and every class of invalid sequence is
 * rejected (overlong, surrogate, out of range, 5-byte, stray continuation,
 * truncated, bad continuation, empty).
 */
#include <stdio.h>
#include <string.h>

#include "nls.h"

/* The 27 non-identity CP1252 bytes, as Unicode constants — stated
 * independently of nls.c's table. */
static const struct {
    uint8_t byte;
    uint16_t uni;
} k_specials_expected[] = {
    { 0x80, 0x20ac }, { 0x82, 0x201a }, { 0x83, 0x0192 }, { 0x84, 0x201e },
    { 0x85, 0x2026 }, { 0x86, 0x2020 }, { 0x87, 0x2021 }, { 0x88, 0x02c6 },
    { 0x89, 0x2030 }, { 0x8a, 0x0160 }, { 0x8b, 0x2039 }, { 0x8c, 0x0152 },
    { 0x8e, 0x017d }, { 0x91, 0x2018 }, { 0x92, 0x2019 }, { 0x93, 0x201c },
    { 0x94, 0x201d }, { 0x95, 0x2022 }, { 0x96, 0x2013 }, { 0x97, 0x2014 },
    { 0x98, 0x02dc }, { 0x99, 0x2122 }, { 0x9a, 0x0161 }, { 0x9b, 0x203a },
    { 0x9c, 0x0153 }, { 0x9e, 0x017e }, { 0x9f, 0x0178 },
};

static const uint8_t k_undefined[] = { 0x81, 0x8d, 0x8f, 0x90, 0x9d };

static bool is_special(uint8_t b)
{
    for (size_t i = 0; i < sizeof k_undefined; i++) {
        if (k_undefined[i] == b) {
            return false;
        }
    }
    return b >= 0x80 && b < 0xa0;
}

static bool check_cp1252_forward(void)
{
    bool ok = true;

    for (unsigned b = 0; b < 256; b++) {
        uint16_t uni = 0xbeef;
        AnemStatus st = anem_cp1252_to_uni((uint8_t)b, &uni);
        if (is_special((uint8_t)b)) {
            /* one of the 27: must match the hardcoded expectation */
            uint16_t want = 0;
            for (size_t i = 0; i < sizeof k_specials_expected / sizeof k_specials_expected[0]; i++) {
                if (k_specials_expected[i].byte == b) {
                    want = k_specials_expected[i].uni;
                }
            }
            if (st != ANEM_OK || uni != want) {
                printf("FAIL cp1252 0x%02x: got U+%04X st %d, want U+%04X\n",
                       b, uni, st, want);
                ok = false;
            }
        } else if (b < 0x80 || b >= 0xa0) {
            /* identity range */
            if (st != ANEM_OK || uni != b) {
                printf("FAIL cp1252 identity 0x%02x: got U+%04X st %d\n",
                       b, uni, st);
                ok = false;
            }
        } else {
            /* one of the five undefined */
            if (st != ANEM_FORMAT) {
                printf("FAIL cp1252 undefined 0x%02x: st %d, want ANEM_FORMAT\n",
                       b, st);
                ok = false;
            }
        }
    }
    printf("%s cp1252 forward (all 256 bytes)\n", ok ? "PASS" : "FAIL");
    return ok;
}

static bool check_cp1252_roundtrip(void)
{
    bool ok = true;

    for (unsigned b = 0; b < 256; b++) {
        uint16_t uni = 0;
        uint8_t back = 0xee;
        if (anem_cp1252_to_uni((uint8_t)b, &uni) != ANEM_OK) {
            continue;               /* undefined: the forward test owns it */
        }
        if (anem_uni_to_cp1252(uni, &back) != ANEM_OK || back != b) {
            printf("FAIL cp1252 roundtrip 0x%02x -> U+%04X -> 0x%02x\n",
                   b, uni, back);
            ok = false;
        }
    }
    {
        uint8_t byte = 0xee;
        if (anem_uni_to_cp1252(0x0100, &byte) != ANEM_FORMAT
            || anem_uni_to_cp1252(0x2603, &byte) != ANEM_FORMAT
            || anem_uni_to_cp1252(0xfffd, &byte) != ANEM_FORMAT) {
            printf("FAIL cp1252: unrepresentable codepoint accepted\n");
            ok = false;
        }
    }
    printf("%s cp1252 roundtrip + unrepresentable\n", ok ? "PASS" : "FAIL");
    return ok;
}

static bool check_utf8_encode(void)
{
    static const struct {
        uint32_t cp;
        uint8_t bytes[4];
        size_t len;
    } k_cases[] = {
        { 0x000000, { 0x00 }, 1 },
        { 0x000041, { 0x41 }, 1 },
        { 0x00007f, { 0x7f }, 1 },
        { 0x0000a2, { 0xc2, 0xa2 }, 2 },
        { 0x0007ff, { 0xdf, 0xbf }, 2 },
        { 0x000800, { 0xe0, 0xa0, 0x80 }, 3 },
        { 0x0020ac, { 0xe2, 0x82, 0xac }, 3 },
        { 0x00ffff, { 0xef, 0xbf, 0xbf }, 3 },
        { 0x010000, { 0xf0, 0x90, 0x80, 0x80 }, 4 },
        { 0x10348, { 0xf0, 0x90, 0x8d, 0x88 }, 4 },
        { 0x10ffff, { 0xf4, 0x8f, 0xbf, 0xbf }, 4 },
    };
    bool ok = true;

    for (size_t i = 0; i < sizeof k_cases / sizeof k_cases[0]; i++) {
        uint8_t out[4] = { 0xcc, 0xcc, 0xcc, 0xcc };
        size_t written = 0;
        if (anem_utf8_encode(k_cases[i].cp, out, &written) != ANEM_OK
            || written != k_cases[i].len
            || memcmp(out, k_cases[i].bytes, written) != 0) {
            printf("FAIL utf8 encode U+%04X\n", (unsigned)k_cases[i].cp);
            ok = false;
        }
    }
    {
        uint8_t out[4];
        size_t written = 12345;
        if (anem_utf8_encode(0xd800u, out, &written) != ANEM_FORMAT
            || anem_utf8_encode(0xdfffu, out, &written) != ANEM_FORMAT
            || anem_utf8_encode(0x110000u, out, &written) != ANEM_FORMAT) {
            printf("FAIL utf8 encode: surrogate or out-of-range accepted\n");
            ok = false;
        }
        if (written != 12345) {
            printf("FAIL utf8 encode: written touched on error\n");
            ok = false;
        }
    }
    printf("%s utf8 encode vectors + rejections\n", ok ? "PASS" : "FAIL");
    return ok;
}

/* Every encodable code point round-trips: encode then decode. */
static bool check_utf8_exhaustive(void)
{
    for (uint32_t cp = 0; cp <= 0x10ffffu; cp++) {
        uint8_t buf[4];
        size_t written = 0;
        uint32_t back = 0;
        size_t consumed = 0;

        if (cp >= 0xd800u && cp <= 0xdfffu) {
            continue;               /* surrogates: owned by the reject tests */
        }
        if (anem_utf8_encode(cp, buf, &written) != ANEM_OK) {
            printf("FAIL exhaustive U+%04X: encode\n", (unsigned)cp);
            return false;
        }
        if (anem_utf8_decode(buf, written, &back, &consumed) != ANEM_OK
            || back != cp || consumed != written) {
            printf("FAIL exhaustive U+%04X: roundtrip\n", (unsigned)cp);
            return false;
        }
    }
    printf("PASS utf8 exhaustive roundtrip (0..10FFFF, surrogates skipped)\n");
    return true;
}

static bool check_utf8_rejects(void)
{
    static const uint8_t k_bad[][4] = {
        { 0xc0, 0x80 },                      /* overlong NUL */
        { 0xe0, 0x80, 0xaf },                /* overlong 3-byte */
        { 0xf0, 0x80, 0x80, 0xaf },          /* overlong 4-byte */
        { 0xf8, 0x88, 0x80, 0x80 },          /* 5-byte form */
        { 0xed, 0xa0, 0x80 },                /* surrogate D800 */
        { 0xed, 0xbf, 0xbf },                /* surrogate DFFF */
        { 0xf4, 0x90, 0x80, 0x80 },          /* above U+10FFFF */
        { 0x80 },                            /* stray continuation */
        { 0xc2 },                            /* truncated 2-byte */
        { 0xe2, 0x82 },                      /* truncated 3-byte */
        { 0xc2, 0x41 },                      /* bad continuation */
    };
    static const size_t k_bad_len[] = { 2, 3, 4, 4, 3, 3, 4, 1, 1, 2, 2 };
    bool ok = true;

    for (size_t i = 0; i < sizeof k_bad_len / sizeof k_bad_len[0]; i++) {
        uint32_t cp = 0;
        size_t consumed = 12345;
        if (anem_utf8_decode(k_bad[i], k_bad_len[i], &cp, &consumed) != ANEM_FORMAT) {
            printf("FAIL utf8 reject case %zu accepted\n", i);
            ok = false;
        }
        if (consumed != 12345) {
            printf("FAIL utf8 reject case %zu: consumed touched\n", i);
            ok = false;
        }
    }
    {
        uint32_t cp = 0;
        size_t consumed = 0;
        if (anem_utf8_decode((const uint8_t *)"", 0, &cp, &consumed) != ANEM_FORMAT) {
            printf("FAIL utf8: empty input accepted\n");
            ok = false;
        }
    }
    printf("%s utf8 strict rejects (%zu cases)\n", ok ? "PASS" : "FAIL",
           sizeof k_bad_len / sizeof k_bad_len[0]);
    return ok;
}

static bool check_utf8_decode_valid(void)
{
    static const struct {
        uint8_t bytes[4];
        size_t len;
        uint32_t cp;
    } k_cases[] = {
        { { 0x41 }, 1, 0x41 },
        { { 0xc2, 0xa2 }, 2, 0xa2 },
        { { 0xe2, 0x82, 0xac }, 3, 0x20ac },
        { { 0xf0, 0x90, 0x8d, 0x88 }, 4, 0x10348 },
    };
    bool ok = true;

    for (size_t i = 0; i < sizeof k_cases / sizeof k_cases[0]; i++) {
        uint32_t cp = 0;
        size_t consumed = 0;
        if (anem_utf8_decode(k_cases[i].bytes, k_cases[i].len, &cp, &consumed) != ANEM_OK
            || cp != k_cases[i].cp || consumed != k_cases[i].len) {
            printf("FAIL utf8 decode case %zu\n", i);
            ok = false;
        }
    }
    printf("%s utf8 decode vectors\n", ok ? "PASS" : "FAIL");
    return ok;
}

static bool check_null_contract(void)
{
    uint16_t uni = 0;
    uint8_t out[4];
    size_t n = 0;
    uint32_t cp = 0;
    bool ok = true;

    ok = (anem_cp1252_to_uni(0x41, NULL) == ANEM_BADARG) && ok;
    ok = (anem_cp1252_to_uni(0x41, &uni) == ANEM_OK) && (uni == 0x41) && ok;
    ok = (anem_uni_to_cp1252(0x41, NULL) == ANEM_BADARG) && ok;
    ok = (anem_utf8_encode(0x41, NULL, &n) == ANEM_BADARG) && ok;
    ok = (anem_utf8_encode(0x41, out, NULL) == ANEM_BADARG) && ok;
    ok = (anem_utf8_decode(NULL, 1, &cp, &n) == ANEM_BADARG) && ok;
    ok = (anem_utf8_decode(out, 1, NULL, &n) == ANEM_BADARG) && ok;
    ok = (anem_utf8_decode(out, 1, &cp, NULL) == ANEM_BADARG) && ok;

    printf("%s null-argument contract\n", ok ? "PASS" : "FAIL");
    return ok;
}

int main(void)
{
    unsigned failures = 0;

    if (!check_cp1252_forward()) { failures++; }
    if (!check_cp1252_roundtrip()) { failures++; }
    if (!check_utf8_encode()) { failures++; }
    if (!check_utf8_decode_valid()) { failures++; }
    if (!check_utf8_exhaustive()) { failures++; }
    if (!check_utf8_rejects()) { failures++; }
    if (!check_null_contract()) { failures++; }

    if (failures != 0) {
        printf("FAILED: %u check(s)\n", failures);
        return 1;
    }
    printf("all checks pass\n");
    return 0;
}
