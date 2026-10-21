/* CryptoAPI-path harness: acquire the provider the way applications do, then
 * drive it through advapi32 rather than calling the CP* entry points directly.
 *
 * This is the P3.5 plumbing check. It proves that CryptoAPI's parameter
 * translation, handle mapping and blob conventions line up with the provider.
 * It is NOT proof against XP: Wine does not enforce the provider-signature
 * check that stock XP applies before CPAcquireContext (docs/P3-CRYPTO.md), so
 * this runs on the host and is labelled as plumbing, not as the XP gate.
 *
 * usage: cryptapi_test.exe
 */
#include <windows.h>
#include <wincrypt.h>
#include <stdio.h>
#include <string.h>

#ifndef CALG_AES_256
#define CALG_AES_256 0x00006610
#endif
#ifndef ZERO_PADDING
#define ZERO_PADDING 3
#endif

static unsigned failures;

static void check(int cond, const char *what)
{
    if (cond) {
        printf("PASS %s\n", what);
    } else {
        printf("FAIL %s\n", what);
        failures++;
    }
}

static BOOL hash_once(HCRYPTPROV prov, ALG_ID alg, const BYTE *data,
                      DWORD len, BYTE *out, DWORD *out_len)
{
    HCRYPTHASH hash = 0;
    BOOL ok;

    if (CryptCreateHash(prov, alg, 0, 0, &hash) == FALSE) {
        return FALSE;
    }
    ok = CryptHashData(hash, data, len, 0) != FALSE;
    if (ok) {
        ok = CryptGetHashParam(hash, HP_HASHVAL, out, out_len, 0) != FALSE;
    }
    CryptDestroyHash(hash);
    return ok;
}

static void check_hash(HCRYPTPROV prov, ALG_ID alg, const char *name,
                       const char *want_hex)
{
    static const BYTE abc[3] = { 'a', 'b', 'c' };
    BYTE digest[64];
    DWORD len = (DWORD)sizeof digest;
    char got[129] = { 0 };
    char label[96];

    if (!hash_once(prov, alg, abc, sizeof abc, digest, &len)) {
        (void)snprintf(label, sizeof label, "%s through CryptoAPI", name);
        check(0, label);
        return;
    }
    for (DWORD i = 0; i < len && i * 2 + 1 < sizeof got; i++) {
        (void)snprintf(got + i * 2, 3, "%02x", digest[i]);
    }
    (void)snprintf(label, sizeof label, "%s through CryptoAPI", name);
    check(strcmp(got, want_hex) == 0, label);
}

static void check_random(HCRYPTPROV prov)
{
    BYTE a[32];
    BYTE b[32];
    unsigned nonzero = 0;

    memset(a, 0, sizeof a);
    memset(b, 0, sizeof b);
    check(CryptGenRandom(prov, sizeof a, a) != FALSE
          && CryptGenRandom(prov, sizeof b, b) != FALSE
          && memcmp(a, b, sizeof a) != 0,
          "CryptGenRandom returns distinct buffers");
    for (unsigned i = 0; i < sizeof a; i++) {
        if (a[i] != 0 || b[i] != 0) {
            nonzero++;
        }
    }
    check(nonzero != 0, "CryptGenRandom output is not all zero");
}

static BOOL import_aes256(HCRYPTPROV prov, const BYTE *key, HCRYPTKEY *out)
{
    /* BLOBHEADER + DWORD cbKey + key bytes. */
    BYTE blob[sizeof(BLOBHEADER) + sizeof(DWORD) + 32];
    BLOBHEADER header;
    DWORD cb = 32;

    memset(&header, 0, sizeof header);
    header.bType = PLAINTEXTKEYBLOB;
    header.bVersion = CUR_BLOB_VERSION;
    header.aiKeyAlg = CALG_AES_256;
    memcpy(blob, &header, sizeof header);
    memcpy(blob + sizeof header, &cb, sizeof cb);
    memcpy(blob + sizeof header + sizeof cb, key, 32);
    return CryptImportKey(prov, blob, (DWORD)sizeof blob, 0, 0, out) != FALSE;
}

static void check_aes(HCRYPTPROV prov)
{
    /* NIST SP 800-38A F.2.5, one block, CBC. */
    static const BYTE key[32] = {
        0x60, 0x3d, 0xeb, 0x10, 0x15, 0xca, 0x71, 0xbe,
        0x2b, 0x73, 0xae, 0xf0, 0x85, 0x7d, 0x77, 0x81,
        0x1f, 0x35, 0x2c, 0x07, 0x3b, 0x61, 0x08, 0xd7,
        0x2d, 0x98, 0x10, 0xa3, 0x09, 0x14, 0xdf, 0xf4,
    };
    static const BYTE iv[16] = {
        0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
        0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f,
    };
    static const BYTE plain[16] = {
        0x6b, 0xc1, 0xbe, 0xe2, 0x2e, 0x40, 0x9f, 0x96,
        0xe9, 0x3d, 0x7e, 0x11, 0x73, 0x93, 0x17, 0x2a,
    };
    static const BYTE want[16] = {
        0xf5, 0x8c, 0x4c, 0x04, 0xd6, 0xe5, 0xf1, 0xba,
        0x77, 0x9e, 0xab, 0xfb, 0x5f, 0x7b, 0xfb, 0xd6,
    };
    HCRYPTKEY key_handle = 0;
    BYTE buf[64];
    DWORD len;
    DWORD mode = CRYPT_MODE_CBC;
    DWORD pad = PKCS5_PADDING;

    if (!import_aes256(prov, key, &key_handle)) {
        check(0, "AES-256 PLAINTEXTKEYBLOB import through CryptoAPI");
        return;
    }
    check(1, "AES-256 PLAINTEXTKEYBLOB import through CryptoAPI");
    check(CryptSetKeyParam(key_handle, KP_MODE, (const BYTE *)&mode, 0) != FALSE,
          "CryptSetKeyParam KP_MODE");
    check(CryptSetKeyParam(key_handle, KP_IV, iv, 0) != FALSE,
          "CryptSetKeyParam KP_IV");

    /* Zero padding on a block-aligned input gives the raw SP 800-38A block. */
    pad = ZERO_PADDING;
    check(CryptSetKeyParam(key_handle, KP_PADDING, (const BYTE *)&pad, 0) != FALSE,
          "CryptSetKeyParam KP_PADDING zero");
    memcpy(buf, plain, sizeof plain);
    len = sizeof plain;
    check(CryptEncrypt(key_handle, 0, TRUE, 0, buf, &len, sizeof buf) != FALSE
          && len == sizeof want && memcmp(buf, want, sizeof want) == 0,
          "AES-256-CBC vector through CryptoAPI");
    check(CryptDecrypt(key_handle, 0, TRUE, 0, buf, &len) != FALSE
          && len == sizeof plain && memcmp(buf, plain, sizeof plain) == 0,
          "AES-256-CBC decrypt through CryptoAPI");

    /* PKCS#5: the same 16 bytes become two blocks and round-trip. */
    pad = PKCS5_PADDING;
    (void)CryptSetKeyParam(key_handle, KP_PADDING, (const BYTE *)&pad, 0);
    memcpy(buf, plain, sizeof plain);
    len = sizeof plain;
    check(CryptEncrypt(key_handle, 0, TRUE, 0, buf, &len, sizeof buf) != FALSE
          && len == 32, "PKCS5 adds a padding block through CryptoAPI");
    check(CryptDecrypt(key_handle, 0, TRUE, 0, buf, &len) != FALSE
          && len == sizeof plain && memcmp(buf, plain, sizeof plain) == 0,
          "PKCS5 round-trip through CryptoAPI");

    CryptDestroyKey(key_handle);
}

/* Cross-provider check of the signature convention. CryptoAPI's rsaenh returns
 * the little-endian form of the PKCS#1 integer; if we returned OpenSSL's
 * big-endian bytes, only our own verify would accept them. Wine's builtin
 * enhanced provider stands in for XP's rsaenh here. */
static void check_cross_provider(HCRYPTKEY ours, const BYTE *sig, DWORD sig_len)
{
    static const BYTE abc[3] = { 'a', 'b', 'c' };
    HCRYPTPROV other = 0;
    HCRYPTKEY other_key = 0;
    HCRYPTHASH hash = 0;
    BYTE blob[512];
    BYTE flipped[256];
    DWORD blob_len = (DWORD)sizeof blob;

    if (sig_len > sizeof flipped) {
        check(0, "signature fits the interop buffer");
        return;
    }
    if (CryptExportKey(ours, 0, PUBLICKEYBLOB, 0, blob, &blob_len) == FALSE) {
        check(0, "public key export for interop");
        return;
    }
    if (CryptAcquireContextA(&other, NULL,
            "Microsoft Enhanced RSA and AES Cryptographic Provider",
            PROV_RSA_AES, CRYPT_VERIFYCONTEXT) == FALSE) {
        check(0, "second CryptoAPI provider available for interop");
        return;
    }
    check(1, "second CryptoAPI provider available for interop");
    if (CryptImportKey(other, blob, blob_len, 0, 0, &other_key) == FALSE) {
        check(0, "public key imports into the second provider");
        CryptReleaseContext(other, 0);
        return;
    }
    check(1, "public key imports into the second provider");
    if (CryptCreateHash(other, CALG_SHA_256, 0, 0, &hash) == FALSE
        || CryptHashData(hash, abc, sizeof abc, 0) == FALSE) {
        check(0, "second provider hashes the digest");
    } else {
        check(1, "second provider hashes the digest");
        check(CryptVerifySignatureA(hash, sig, sig_len, other_key, NULL, 0) != FALSE,
              "our signature verifies in the other CryptoAPI provider");
        memcpy(flipped, sig, sig_len);
        for (DWORD i = 0; i < sig_len / 2; i++) {
            BYTE tmp = flipped[i];
            flipped[i] = flipped[sig_len - 1 - i];
            flipped[sig_len - 1 - i] = tmp;
        }
        check(CryptVerifySignatureA(hash, flipped, sig_len, other_key, NULL, 0) == FALSE,
              "byte-reversed signature is rejected by the other provider");
    }
    if (hash != 0) {
        CryptDestroyHash(hash);
    }
    CryptDestroyKey(other_key);
    CryptReleaseContext(other, 0);
}

static void check_rsa(HCRYPTPROV prov)
{
    static const BYTE abc[3] = { 'a', 'b', 'c' };
    HCRYPTKEY key = 0;
    HCRYPTHASH sign_hash = 0;
    HCRYPTHASH verify_hash = 0;
    BYTE sig[256];
    DWORD sig_len = (DWORD)sizeof sig;

    if (CryptGetUserKey(prov, AT_SIGNATURE, &key) == FALSE) {
        check(0, "CryptGetUserKey AT_SIGNATURE");
        return;
    }
    check(1, "CryptGetUserKey AT_SIGNATURE");
    check(CryptCreateHash(prov, CALG_SHA_256, 0, 0, &sign_hash) != FALSE
          && CryptHashData(sign_hash, abc, sizeof abc, 0) != FALSE,
          "RSA signing hash");
    check(CryptSignHashA(sign_hash, AT_SIGNATURE, NULL, 0, sig, &sig_len) != FALSE
          && sig_len != 0, "CryptSignHash through CryptoAPI");
    check(CryptCreateHash(prov, CALG_SHA_256, 0, 0, &verify_hash) != FALSE
          && CryptHashData(verify_hash, abc, sizeof abc, 0) != FALSE,
          "RSA verifying hash");
    check(CryptVerifySignatureA(verify_hash, sig, sig_len, key, NULL, 0) != FALSE,
          "CryptVerifySignature accepts the signature");
    sig[0] ^= 0x01;
    check(CryptVerifySignatureA(verify_hash, sig, sig_len, key, NULL, 0) == FALSE,
          "CryptVerifySignature rejects a tampered signature");
    sig[0] ^= 0x01;
    check_cross_provider(key, sig, sig_len);

    CryptDestroyHash(verify_hash);
    CryptDestroyHash(sign_hash);
    CryptDestroyKey(key);
}

int main(void)
{
    HCRYPTPROV prov = 0;
    char name[128] = { 0 };
    DWORD name_len = (DWORD)sizeof name;

    if (CryptAcquireContextA(&prov, NULL, "Anemos OpenSSL CSP",
                             PROV_RSA_FULL, CRYPT_VERIFYCONTEXT) == FALSE) {
        printf("FAIL CryptAcquireContext (error %lu)\n", GetLastError());
        return 1;
    }
    check(1, "CryptAcquireContext");
    if (CryptGetProvParam(prov, PP_NAME, (BYTE *)name, &name_len, 0) != FALSE) {
        check(strstr(name, "Anemos") != NULL, "PP_NAME through CryptoAPI");
    } else {
        check(0, "PP_NAME through CryptoAPI");
    }

    check_hash(prov, CALG_MD5, "MD5", "900150983cd24fb0d6963f7d28e17f72");
#ifdef ANEM_CSP_NEGATIVE
    check_hash(prov, CALG_SHA1, "SHA-1", "0000000000000000000000000000000000000000");
#else
    check_hash(prov, CALG_SHA1, "SHA-1", "a9993e364706816aba3e25717850c26c9cd0d89d");
#endif
    check_hash(prov, CALG_SHA_256, "SHA-256",
               "ba7816bf8f01cfea414140de5dae2223"
               "b00361a396177a9cb410ff61f20015ad");
    check_random(prov);
    check_aes(prov);
    check_rsa(prov);

    CryptReleaseContext(prov, 0);
    if (failures != 0u) {
        printf("FAILED: %u check(s)\n", failures);
        return 1;
    }
    printf("all CryptoAPI-path checks pass\n");
    return 0;
}
