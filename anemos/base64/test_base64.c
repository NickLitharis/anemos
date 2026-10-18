/* Vector suite for anemos/base64.
 *
 * The first seven cases are RFC 4648 §10's own published vectors; the three
 * longer ones pin real binary data (an LCG stream regenerated in C, expected
 * strings from Python's base64 — see vectors.py).
 *
 * The suite pins, in this order:
 *   1  encode matches the golden strings exactly (alphabet + padding);
 *   2  decode reproduces the golden bytes exactly;
 *   3  every length 0..1024 round-trips (all three padding residues);
 *   4  malformed input is rejected with ANEM_FORMAT and the output buffer
 *      and *written left untouched (all-or-nothing);
 *   5  a short output buffer is ANEM_NOSPACE, again touching nothing;
 *   6  the null/size/overflow contract.
 */
#include <stdio.h>
#include <string.h>

#include "base64.h"

struct b64_vector {
    const char *name;
    size_t len;
    const char *text;       /* message bytes; NULL => the LCG stream */
    const char *b64;
};

static const struct b64_vector k_vectors[] = {
    { "rfc_empty",  0,   "",     "" },
    { "rfc_f",      1,   "f",    "Zg==" },
    { "rfc_fo",     2,   "fo",   "Zm8=" },
    { "rfc_foo",    3,   "foo",  "Zm9v" },
    { "rfc_foob",   4,   "foob", "Zm9vYg==" },
    { "rfc_fooba",  5,   "fooba", "Zm9vYmE=" },
    { "rfc_foobar", 6,   "foobar", "Zm9vYmFy" },
    { "lcg_64",     64,  NULL,
      "xn6Ba0v74vtU9r3ffBzhhwG/Md5Wcg9HZ2aHWaqIPFnqVhN70oWh2DxUVS83rmVb2gJ5mMzj"
      "GnaOX9mZjx8/Ng==" },
    { "lcg_255",    255, NULL,
      "xn6Ba0v74vtU9r3ffBzhhwG/Md5Wcg9HZ2aHWaqIPFnqVhN70oWh2DxUVS83rmVb2gJ5mMzj"
      "GnaOX9mZjx8/Nu5DeE0N+r6m2uSGjtwpbU7/VuFwIPuPsVgFkMUJ3FPNqjtImVLTUp0Gn+q1"
      "wgYTmEmyAR6sMogxnFJGlXE2j1f2OR0W+oh09Zh8F1xBu21xjg9wWccBGy8zPZHAHaUNDasz"
      "jX5ejz7maHSmOrHDkxGoZMfbyuBg4fO/CQBnouMloCExh9VixahPfi4Ja5SfsG2pnloLRnCA"
      "ts9HDKalKtis+6Drt3kkciOSSIDFpqeFt9eMkOSrY0RSZuOcMyX5" },
    { "lcg_1000",   1000, NULL,
      "xn6Ba0v74vtU9r3ffBzhhwG/Md5Wcg9HZ2aHWaqIPFnqVhN70oWh2DxUVS83rmVb2gJ5mMzj"
      "GnaOX9mZjx8/Nu5DeE0N+r6m2uSGjtwpbU7/VuFwIPuPsVgFkMUJ3FPNqjtImVLTUp0Gn+q1"
      "wgYTmEmyAR6sMogxnFJGlXE2j1f2OR0W+oh09Zh8F1xBu21xjg9wWccBGy8zPZHAHaUNDasz"
      "jX5ejz7maHSmOrHDkxGoZMfbyuBg4fO/CQBnouMloCExh9VixahPfi4Ja5SfsG2pnloLRnCA"
      "ts9HDKalKtis+6Drt3kkciOSSIDFpqeFt9eMkOSrY0RSZuOcMyX5Xqq6c2BdS3F+vqmMVxlx"
      "w8pe5SozrIhRZqF7dWdkmmnvb1ZCoB1RxQL3u5JFvm8NtjjMEP27VFEceweUJ5N9ksPUxqVh"
      "UQE4OKe/8QQNFZuAH4PVpGmIfJ+2AdqTF0WLErICM1xQ1uFWpK1CSlzdhmHpAxLhD5vqJixh"
      "3GJIa20U4AOFSnJG2pbIfRzRBT7lknBDX2wDBbPrsyA1TX5mUAE2wDPhD8k4LukpGU9esdFJ"
      "iztT/Z8/7iUlNXsNEa9MEYwy1Np/2BZX4abOfcGuYr8T5IdMOsGzDFmZR1havXh8ulAB7Rvq"
      "ikmI7tYUhauwLN41kxEtARzXKEMw57AI7XmZE1HSOnetPbT4x8oDItLJxicPBM56P8BoLM9y"
      "agnCQgByXkE0+JZpP706WJGL4cyisZLdd6E1/vNLvLHjNxENx2W+8WHlXgb/Ncd2iV30bkrM"
      "tVR+8RXIoJmPXHAL7xTG5QqcGbQdTM5WBtxCESXnlm8PIT3f+VdHDd8ravx3jdXp2fm14Oty"
      "hBqOQhQdim5fkjr7C+X25MCfRdYqg7+xzWrEv4ze37L3efdgV/w7PXsuy5xBeyel40hYFQcX"
      "4LmFX2Oo9ikSQwBq2+5kJFKLxDtduzUYotOJ/7KgWTDy29XBTWpLNpxdeObQo5IN5ZARsIYP"
      "QTSApom96S94Rw1QlYcbv+N/lDc25G85OC8MgzqF31G8SNlWu3mVeb3USFCdqWVdF3wTCxJc"
      "T2ewBOGeGLMAOv7LxBz3K1A4fk67E8Ugw/49pDAP5EcK5FIBeheBMYCAXzVaLRXMsCIVLYDR"
      "5uTMWK9vBX2FnDVqdKDwKE/3+dw4ALPE7lRO8dnqrcLX6xkkxFaoi8tUa69wWFoHWf4ABt+h"
      "5hhZusFbI/xbHnAwQhrU0DJykGZCbJ2i0e13PjC2rpINYS72ohpJ26Ediaje8jhWumurylNa"
      "U/ZtE4GuH6X8Sj3XRQGJ5KQAmPb7TYZkRl9ZrA==" },
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

static void make_msg(const struct b64_vector *vec, uint8_t *buf)
{
    if (vec->text != NULL) {
        memcpy(buf, vec->text, vec->len);
    } else {
        lcg_fill(buf, vec->len);
    }
}

static uint8_t g_sentinel[1400];

static void fill_sentinel(void)
{
    memset(g_sentinel, 0xcc, sizeof g_sentinel);
}

static bool sentinel_intact(void)
{
    for (size_t i = 0; i < sizeof g_sentinel; i++) {
        if (g_sentinel[i] != 0xccu) {
            return false;
        }
    }
    return true;
}

static bool check_vector(const struct b64_vector *vec)
{
    static uint8_t msg[1024];
    static char enc[1400];
    static uint8_t dec[1024];
    const size_t b64_len = strlen(vec->b64);
    size_t written = 0;
    bool ok = true;

    if (vec->len > sizeof msg) {
        printf("FAIL %s: message buffer too small\n", vec->name);
        return false;
    }
    make_msg(vec, msg);

    if (anem_b64_encode(msg, vec->len, enc, sizeof enc, &written) != ANEM_OK) {
        printf("FAIL %s: encode status\n", vec->name);
        return false;
    }
    ok = (written == b64_len) && ok;
    if (strncmp(enc, vec->b64, b64_len) != 0) {
        printf("FAIL %s: encode got \"%.*s\" want \"%s\"\n",
               vec->name, (int)b64_len, enc, vec->b64);
        ok = false;
    }

    if (anem_b64_decode(vec->b64, b64_len, dec, sizeof dec, &written) != ANEM_OK) {
        printf("FAIL %s: decode status\n", vec->name);
        return false;
    }
    ok = (written == vec->len) && ok;
    if (memcmp(dec, msg, vec->len) != 0) {
        printf("FAIL %s: decode bytes differ\n", vec->name);
        ok = false;
    }

    if (ok) {
        printf("PASS %s (%zu bytes <-> %zu chars)\n", vec->name, vec->len, b64_len);
    }
    return ok;
}

/* Every length 0..1024 covers all three padding residues, repeatedly. */
static bool check_roundtrip_sweep(void)
{
    static uint8_t msg[1024];
    static char enc[1400];
    static uint8_t dec[1024];

    for (size_t n = 0; n <= sizeof msg; n++) {
        size_t written = 0;
        size_t want_len = 4u * ((n + 2u) / 3u);

        lcg_fill(msg, n);
        if (anem_b64_encode(msg, n, enc, sizeof enc, &written) != ANEM_OK
            || written != want_len) {
            printf("FAIL sweep %zu: encode (written %zu, want %zu)\n",
                   n, written, want_len);
            return false;
        }
        if (anem_b64_decode(enc, written, dec, sizeof dec, &written) != ANEM_OK
            || written != n
            || memcmp(dec, msg, n) != 0) {
            printf("FAIL sweep %zu: decode roundtrip\n", n);
            return false;
        }
    }
    printf("PASS roundtrip sweep (0..%zu bytes)\n", sizeof msg);
    return true;
}

/* Malformed input: ANEM_FORMAT, and neither out nor *written is touched. */
static bool check_rejects(void)
{
    static const char *const k_bad[] = {
        "A", "AAA", "AAAAA",              /* length not a multiple of four */
        "AAA!", "Zm 9", "Zm9\n",          /* outside the alphabet (ws too) */
        "=AAA", "A=AA", "AA=A",           /* '=' outside the final group */
        "A===", "====", "Zm9vYg=A",       /* too much padding, padding inside */
        "Zh==", "Zm9=",                   /* non-canonical trailing bits */
    };
    bool ok = true;

    for (size_t i = 0; i < sizeof k_bad / sizeof k_bad[0]; i++) {
        size_t written = 12345;
        fill_sentinel();
        if (anem_b64_decode(k_bad[i], strlen(k_bad[i]),
                            g_sentinel, sizeof g_sentinel, &written)
            != ANEM_FORMAT) {
            printf("FAIL reject \"%s\": not ANEM_FORMAT\n", k_bad[i]);
            ok = false;
        }
        if (!sentinel_intact()) {
            printf("FAIL reject \"%s\": output buffer was modified\n", k_bad[i]);
            ok = false;
        }
        if (written != 12345) {
            printf("FAIL reject \"%s\": written was modified\n", k_bad[i]);
            ok = false;
        }
    }
    if (ok) {
        printf("PASS strict decode (%zu malformed inputs rejected)\n",
               sizeof k_bad / sizeof k_bad[0]);
    }
    return ok;
}

/* Short output buffer: ANEM_NOSPACE, again touching nothing. */
static bool check_nospace(void)
{
    size_t written = 12345;
    bool ok = true;

    fill_sentinel();                    /* "foo" needs 4, offer 3 */
    if (anem_b64_encode("foo", 3, (char *)g_sentinel, 3, &written) != ANEM_NOSPACE) {
        printf("FAIL nospace encode: not ANEM_NOSPACE\n");
        ok = false;
    }
    ok = sentinel_intact() && ok;
    ok = (written == 12345) && ok;

    written = 12345;
    fill_sentinel();                    /* "Zm9vYmFy" yields 6, offer 5 */
    if (anem_b64_decode("Zm9vYmFy", 8, g_sentinel, 5, &written) != ANEM_NOSPACE) {
        printf("FAIL nospace decode: not ANEM_NOSPACE\n");
        ok = false;
    }
    ok = sentinel_intact() && ok;
    ok = (written == 12345) && ok;

    printf("%s short-buffer contract\n", ok ? "PASS" : "FAIL");
    return ok;
}

static bool check_contract(void)
{
    uint8_t out[8];
    size_t n = 0;
    size_t written = 0;
    bool ok = true;

    ok = (anem_b64_encoded_size(0, &n) == ANEM_OK) && (n == 0) && ok;
    ok = (anem_b64_encoded_size(1, &n) == ANEM_OK) && (n == 4) && ok;
    ok = (anem_b64_encoded_size(2, &n) == ANEM_OK) && (n == 4) && ok;
    ok = (anem_b64_encoded_size(3, &n) == ANEM_OK) && (n == 4) && ok;
    ok = (anem_b64_encoded_size(4, &n) == ANEM_OK) && (n == 8) && ok;
    ok = (anem_b64_encoded_size(SIZE_MAX, &n) == ANEM_OVERFLOW) && ok;
    ok = (anem_b64_encoded_size(1, NULL) == ANEM_BADARG) && ok;

    ok = (anem_b64_encode(NULL, 1, (char *)out, 4, &written) == ANEM_BADARG) && ok;
    ok = (anem_b64_encode(out, 3, NULL, 4, &written) == ANEM_BADARG) && ok;
    ok = (anem_b64_encode(out, 3, (char *)out, 4, NULL) == ANEM_BADARG) && ok;
    ok = (anem_b64_encode(NULL, 0, NULL, 0, &written) == ANEM_OK)
         && (written == 0) && ok;

    ok = (anem_b64_decode(NULL, 4, out, 8, &written) == ANEM_BADARG) && ok;
    ok = (anem_b64_decode("Zm9v", 4, NULL, 3, &written) == ANEM_BADARG) && ok;
    ok = (anem_b64_decode("Zm9v", 4, out, 8, NULL) == ANEM_BADARG) && ok;
    ok = (anem_b64_decode("Zm9v", 0, out, 8, &written) == ANEM_OK)
         && (written == 0) && ok;

    printf("%s null/size/overflow contract\n", ok ? "PASS" : "FAIL");
    return ok;
}

int main(void)
{
    unsigned failures = 0;

    for (size_t v = 0; v < sizeof k_vectors / sizeof k_vectors[0]; v++) {
        if (!check_vector(&k_vectors[v])) {
            failures++;
        }
    }
    if (!check_roundtrip_sweep()) {
        failures++;
    }
    if (!check_rejects()) {
        failures++;
    }
    if (!check_nospace()) {
        failures++;
    }
    if (!check_contract()) {
        failures++;
    }

    if (failures != 0) {
        printf("FAILED: %u check(s)\n", failures);
        return 1;
    }
    printf("all checks pass\n");
    return 0;
}
