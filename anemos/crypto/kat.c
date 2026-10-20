/* Known-answer tests for the OpenSSL build the P3 CSP will link.
 *
 * The point is not "OpenSSL works" — it is "this exact i686 Windows build,
 * produced by the pinned toolchain, produces the published answer for each
 * algorithm the CSP must expose, and runs under the same PE loader XP uses".
 *
 * Vectors: FIPS 180 (SHA-1/256/512), RFC 4231 case 1 (HMAC-SHA-256),
 * NIST SP 800-38A F.2.1 (AES-128-CBC, no padding) and the GCM spec's
 * all-zero 256-bit key / 96-bit IV / 128-bit plaintext case.
 *
 * RSA has no fixed answer because key generation is random; sign/verify and
 * tamper-rejection are checked instead.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <openssl/core_names.h>
#include <openssl/evp.h>
#include <openssl/opensslv.h>
#include <openssl/params.h>
#include <openssl/rsa.h>

static unsigned failures;

static void fail(const char *what)
{
    printf("FAIL %s\n", what);
    failures++;
}

static void hex_encode(char *dst, const unsigned char *src, size_t n)
{
    static const char digits[] = "0123456789abcdef";

    for (size_t i = 0; i < n; i++) {
        dst[2 * i] = digits[src[i] >> 4];
        dst[2 * i + 1] = digits[src[i] & 0x0fu];
    }
    dst[2 * n] = '\0';
}

static void expect_hex(const char *name, const unsigned char *got, size_t n,
                       const char *want)
{
    char text[2 * EVP_MAX_MD_SIZE + 1];

    hex_encode(text, got, n);
    if (strcmp(text, want) == 0) {
        printf("PASS %s\n", name);
    } else {
        printf("FAIL %s: got %s want %s\n", name, text, want);
        failures++;
    }
}

static void kat_digest(const char *name, const EVP_MD *md, const void *data,
                       size_t len, const char *want)
{
    unsigned char out[EVP_MAX_MD_SIZE];
    unsigned int out_len = 0;

    if (EVP_Digest(data, len, out, &out_len, md, NULL) != 1) {
        fail(name);
        return;
    }
    expect_hex(name, out, out_len, want);
}

static void kat_hmac_sha256(const void *key, size_t key_len, const void *data,
                            size_t data_len, const char *want)
{
    EVP_MAC *mac = EVP_MAC_fetch(NULL, "HMAC", NULL);
    EVP_MAC_CTX *ctx = (mac != NULL) ? EVP_MAC_CTX_new(mac) : NULL;
    OSSL_PARAM params[2];
    char digest[] = "SHA256";
    unsigned char out[EVP_MAX_MD_SIZE];
    size_t out_len = 0;
    int ok;

    params[0] = OSSL_PARAM_construct_utf8_string(OSSL_MAC_PARAM_DIGEST, digest,
                                                 0);
    params[1] = OSSL_PARAM_construct_end();
    ok = ctx != NULL
      && EVP_MAC_init(ctx, key, key_len, params) == 1
      && EVP_MAC_update(ctx, data, data_len) == 1
      && EVP_MAC_final(ctx, out, &out_len, sizeof out) == 1;
    if (!ok) {
        fail("hmac-sha256");
    } else {
        expect_hex("hmac-sha256", out, out_len, want);
    }
    EVP_MAC_CTX_free(ctx);
    EVP_MAC_free(mac);
}

static void kat_aes128_cbc(const unsigned char key[16],
                           const unsigned char iv[16],
                           const unsigned char plain[16], const char *want)
{
    EVP_CIPHER_CTX *ctx = EVP_CIPHER_CTX_new();
    unsigned char out[32];
    int len1 = 0;
    int len2 = 0;
    int ok = ctx != NULL
          && EVP_EncryptInit_ex(ctx, EVP_aes_128_cbc(), NULL, key, iv) == 1
          && EVP_CIPHER_CTX_set_padding(ctx, 0) == 1
          && EVP_EncryptUpdate(ctx, out, &len1, plain, 16) == 1
          && EVP_EncryptFinal_ex(ctx, out + len1, &len2) == 1;

    if (!ok) {
        fail("aes-128-cbc");
    } else {
        expect_hex("aes-128-cbc", out, (size_t)(len1 + len2), want);
    }
    EVP_CIPHER_CTX_free(ctx);
}

static void kat_aes256_gcm(const unsigned char key[32],
                           const unsigned char iv[12],
                           const unsigned char plain[16])
{
    EVP_CIPHER_CTX *ctx = EVP_CIPHER_CTX_new();
    unsigned char out[32];
    unsigned char tag[16];
    int len1 = 0;
    int len2 = 0;
    int ok = ctx != NULL
          && EVP_EncryptInit_ex(ctx, EVP_aes_256_gcm(), NULL, NULL, NULL) == 1
          && EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_IVLEN, 12, NULL) == 1
          && EVP_EncryptInit_ex(ctx, NULL, NULL, key, iv) == 1
          && EVP_EncryptUpdate(ctx, out, &len1, plain, 16) == 1
          && EVP_EncryptFinal_ex(ctx, out + len1, &len2) == 1
          && EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_GET_TAG, 16, tag) == 1;

    if (!ok) {
        fail("aes-256-gcm");
    } else {
        expect_hex("aes-256-gcm ciphertext", out, (size_t)(len1 + len2),
                   "cea7403d4d606b6e074ec5d3baf39d18");
        expect_hex("aes-256-gcm tag", tag, sizeof tag,
                   "d0d1c8a799996bf0265b98b5d48ab919");
    }
    EVP_CIPHER_CTX_free(ctx);
}

static void kat_rsa_sha256(void)
{
    static const unsigned char msg[16] = "anemos rsa test";
    EVP_PKEY_CTX *pctx = EVP_PKEY_CTX_new_id(EVP_PKEY_RSA, NULL);
    EVP_PKEY *key = NULL;
    EVP_MD_CTX *mctx = EVP_MD_CTX_new();
    unsigned char *sig = NULL;
    size_t sig_len = 0;
    int ok = pctx != NULL && mctx != NULL
          && EVP_PKEY_keygen_init(pctx) == 1
          && EVP_PKEY_CTX_set_rsa_keygen_bits(pctx, 1024) == 1
          && EVP_PKEY_keygen(pctx, &key) == 1
          && EVP_DigestSignInit(mctx, NULL, EVP_sha256(), NULL, key) == 1
          && EVP_DigestSign(mctx, NULL, &sig_len, msg, sizeof msg) == 1;

    sig = ok ? (unsigned char *)malloc(sig_len) : NULL;
    ok = ok && sig != NULL
      && EVP_DigestSignInit(mctx, NULL, EVP_sha256(), NULL, key) == 1
      && EVP_DigestSign(mctx, sig, &sig_len, msg, sizeof msg) == 1;
    if (!ok) {
        fail("rsa-1024 sign");
    } else if (EVP_DigestVerifyInit(mctx, NULL, EVP_sha256(), NULL, key) != 1
               || EVP_DigestVerify(mctx, sig, sig_len, msg, sizeof msg) != 1) {
        fail("rsa-1024 verify");
    } else {
        printf("PASS rsa-1024 sign/verify\n");
        sig[0] ^= 0x01u;
        if (EVP_DigestVerifyInit(mctx, NULL, EVP_sha256(), NULL, key) != 1
            || EVP_DigestVerify(mctx, sig, sig_len, msg, sizeof msg) == 1) {
            fail("rsa-1024 tampered signature is rejected");
        } else {
            printf("PASS rsa-1024 tampered signature is rejected\n");
        }
    }
    free(sig);
    EVP_MD_CTX_free(mctx);
    EVP_PKEY_free(key);
    EVP_PKEY_CTX_free(pctx);
}

#ifdef ANEM_CRYPTO_NEGATIVE
/* Negative control: a wrong expected digest must make the suite fail. */
#define KAT_SHA256 \
    "0000000000000000000000000000000000000000000000000000000000000000"
#else
#define KAT_SHA256 \
    "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"
#endif

int main(void)
{
    static const unsigned char hmac_key[20] = {
        0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b,
        0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b
    };
    static const unsigned char cbc_key[16] = {
        0x2b, 0x7e, 0x15, 0x16, 0x28, 0xae, 0xd2, 0xa6,
        0xab, 0xf7, 0x15, 0x88, 0x09, 0xcf, 0x4f, 0x3c
    };
    static const unsigned char cbc_iv[16] = {
        0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
        0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f
    };
    static const unsigned char cbc_plain[16] = {
        0x6b, 0xc1, 0xbe, 0xe2, 0x2e, 0x40, 0x9f, 0x96,
        0xe9, 0x3d, 0x7e, 0x11, 0x73, 0x93, 0x17, 0x2a
    };
    static const unsigned char zero32[32] = { 0 };
    static const unsigned char zero12[12] = { 0 };
    static const unsigned char zero16[16] = { 0 };

    printf("OpenSSL: %s\n", OpenSSL_version(OPENSSL_VERSION));
    kat_digest("sha1-abc", EVP_sha1(), "abc", 3,
               "a9993e364706816aba3e25717850c26c9cd0d89d");
    kat_digest("sha256-abc", EVP_sha256(), "abc", 3, KAT_SHA256);
    kat_digest("sha512-abc", EVP_sha512(), "abc", 3,
               "ddaf35a193617abacc417349ae20413112e6fa4e89a97ea20a9eeee64b55d39a"
               "2192992a274fc1a836ba3c23a3feebbd454d4423643ce80e2a9ac94fa54ca49f");
    kat_hmac_sha256(hmac_key, sizeof hmac_key, "Hi There", 8,
                    "b0344c61d8db38535ca8afceaf0bf12b"
                    "881dc200c9833da726e9376c2e32cff7");
    kat_aes128_cbc(cbc_key, cbc_iv, cbc_plain,
                   "7649abac8119b246cee98e9b12e9197d");
    kat_aes256_gcm(zero32, zero12, zero16);
    kat_rsa_sha256();

    if (failures != 0u) {
        printf("FAILED: %u check(s)\n", failures);
        return 1;
    }
    printf("all OpenSSL known-answer tests pass\n");
    return 0;
}
