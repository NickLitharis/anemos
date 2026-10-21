/* CSP SPI harness: load the provider DLL and call it the way CryptoAPI would,
 * but without the provider-signature check that stock XP applies before
 * CryptAcquireContext. This is the P3.1 gate.
 *
 * Checks: every required export resolves; contexts acquire and release;
 * CPGenRandom produces distinct non-zero bytes; MD5/SHA-1/SHA-256 match
 * published answers through CPCreateHash/CPHashData/CPGetHashParam; provider
 * properties answer; unsupported key operations fail explicitly.
 *
 * usage: spi_test.exe anemosp.dll [result-file]
 *
 * The optional result file mirrors every PASS/FAIL line. The in-guest run
 * (P3.4) has no console the host can read, so it passes
 * `\conftest\spi.log` and the conformance runner collects that file.
 */
#include <io.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "csp_abi.h"

static unsigned failures;
static FILE *record;   /* guest result file, NULL on the host run */

static void check(int cond, const char *what)
{
    if (cond) {
        printf("PASS %s\n", what);
        if (record != NULL) {
            fprintf(record, "PASS %s\n", what);
        }
    } else {
        printf("FAIL %s\n", what);
        if (record != NULL) {
            fprintf(record, "FAIL %s\n", what);
        }
        failures++;
    }
}

static void hex_encode(char *dst, const BYTE *src, DWORD n)
{
    static const char digits[] = "0123456789abcdef";

    for (DWORD i = 0; i < n; i++) {
        dst[2 * i] = digits[src[i] >> 4];
        dst[2 * i + 1] = digits[src[i] & 0x0fu];
    }
    dst[2 * n] = '\0';
}

typedef BOOL (WINAPI *FnAcquire)(HCRYPTPROV *, LPCSTR, DWORD,
                                 AnemVTableProvStruc *);
typedef BOOL (WINAPI *FnRelease)(HCRYPTPROV, DWORD);
typedef BOOL (WINAPI *FnGenRandom)(HCRYPTPROV, DWORD, LPBYTE);
typedef BOOL (WINAPI *FnCreateHash)(HCRYPTPROV, ALG_ID, HCRYPTKEY, DWORD,
                                    HCRYPTHASH *);
typedef BOOL (WINAPI *FnHashData)(HCRYPTPROV, HCRYPTHASH, const BYTE *, DWORD,
                                  DWORD);
typedef BOOL (WINAPI *FnGetHashParam)(HCRYPTPROV, HCRYPTHASH, DWORD, LPBYTE,
                                      LPDWORD, DWORD);
typedef BOOL (WINAPI *FnDestroyHash)(HCRYPTPROV, HCRYPTHASH);
typedef BOOL (WINAPI *FnDuplicateHash)(HCRYPTPROV, HCRYPTHASH, LPDWORD, DWORD,
                                       HCRYPTHASH *);
typedef BOOL (WINAPI *FnGetProvParam)(HCRYPTPROV, DWORD, LPBYTE, LPDWORD,
                                      DWORD);
typedef BOOL (WINAPI *FnGenKey)(HCRYPTPROV, ALG_ID, DWORD, HCRYPTKEY *);
typedef BOOL (WINAPI *FnSignHash)(HCRYPTPROV, HCRYPTHASH, DWORD, LPCWSTR,
                                  DWORD, LPBYTE, LPDWORD);
typedef BOOL (WINAPI *FnDeriveKey)(HCRYPTPROV, ALG_ID, HCRYPTHASH, DWORD,
                                   HCRYPTKEY *);
typedef BOOL (WINAPI *FnDestroyKey)(HCRYPTPROV, HCRYPTKEY);
typedef BOOL (WINAPI *FnGetKeyParam)(HCRYPTPROV, HCRYPTKEY, DWORD, LPBYTE,
                                     LPDWORD, DWORD);
typedef BOOL (WINAPI *FnSetKeyParam)(HCRYPTPROV, HCRYPTKEY, DWORD,
                                     const BYTE *, DWORD);
typedef BOOL (WINAPI *FnImportKey)(HCRYPTPROV, const BYTE *, DWORD, HCRYPTKEY,
                                   DWORD, HCRYPTKEY *);
typedef BOOL (WINAPI *FnExportKey)(HCRYPTPROV, HCRYPTKEY, HCRYPTKEY, DWORD,
                                   DWORD, LPBYTE, LPDWORD);
typedef BOOL (WINAPI *FnEncrypt)(HCRYPTPROV, HCRYPTKEY, HCRYPTHASH, BOOL, DWORD,
                                 LPBYTE, LPDWORD, DWORD);
typedef BOOL (WINAPI *FnDecrypt)(HCRYPTPROV, HCRYPTKEY, HCRYPTHASH, BOOL, DWORD,
                                 LPBYTE, LPDWORD);
typedef BOOL (WINAPI *FnDuplicateKey)(HCRYPTPROV, HCRYPTKEY, LPDWORD, DWORD,
                                      HCRYPTKEY *);
typedef BOOL (WINAPI *FnGetUserKey)(HCRYPTPROV, DWORD, HCRYPTKEY *);
typedef BOOL (WINAPI *FnVerifySignature)(HCRYPTPROV, HCRYPTHASH, const BYTE *,
                                         DWORD, HCRYPTKEY, LPCWSTR, DWORD);

static FnAcquire pAcquire;
static FnRelease pRelease;
static FnGenRandom pGenRandom;
static FnCreateHash pCreateHash;
static FnHashData pHashData;
static FnGetHashParam pGetHashParam;
static FnDestroyHash pDestroyHash;
static FnDuplicateHash pDuplicateHash;
static FnGetProvParam pGetProvParam;
static FnGenKey pGenKey;
static FnSignHash pSignHash;
static FnDeriveKey pDeriveKey;
static FnDestroyKey pDestroyKey;
static FnGetKeyParam pGetKeyParam;
static FnSetKeyParam pSetKeyParam;
static FnImportKey pImportKey;
static FnExportKey pExportKey;
static FnEncrypt pEncrypt;
static FnDecrypt pDecrypt;
static FnDuplicateKey pDuplicateKey;
static FnGetUserKey pGetUserKey;
static FnVerifySignature pVerifySignature;

static const char *k_required[] = {
    "CPAcquireContext", "CPReleaseContext", "CPGenKey", "CPDeriveKey",
    "CPDestroyKey", "CPSetKeyParam", "CPGetKeyParam", "CPExportKey",
    "CPImportKey", "CPEncrypt", "CPDecrypt", "CPCreateHash", "CPHashData",
    "CPHashSessionKey", "CPDestroyHash", "CPSignHash", "CPVerifySignature",
    "CPGenRandom", "CPGetUserKey", "CPSetProvParam", "CPGetProvParam",
    "CPSetHashParam", "CPGetHashParam",
};

static void check_exports(HMODULE dll)
{
    unsigned missing = 0;

    for (unsigned i = 0; i < sizeof k_required / sizeof k_required[0]; i++) {
        if (GetProcAddress(dll, k_required[i]) == NULL) {
            printf("  missing export: %s\n", k_required[i]);
            missing++;
        }
    }
    check(missing == 0, "all 23 required SPI exports resolve");
    check(GetProcAddress(dll, "CPDuplicateKey") != NULL
          && GetProcAddress(dll, "CPDuplicateHash") != NULL,
          "both optional exports resolve");
}

static void check_hash(HCRYPTPROV prov, ALG_ID alg, const char *name,
                       const char *want)
{
    HCRYPTHASH hash = 0;
    HCRYPTHASH copy = 0;
    DWORD size = 0;
    DWORD value_len = 0;
    BYTE value[64];
    char got[129];
    char label[96];

    (void)snprintf(label, sizeof label, "%s create", name);
    check(pCreateHash(prov, alg, 0, 0, &hash) == TRUE && hash != 0, label);

    (void)snprintf(label, sizeof label, "%s data", name);
    check(pHashData(prov, hash, (const BYTE *)"abc", 3, 0) == TRUE, label);

    (void)snprintf(label, sizeof label, "%s hash size query", name);
    check(pGetHashParam(prov, hash, HP_HASHSIZE, NULL, &size, 0) == FALSE
          && GetLastError() == (DWORD)ERROR_MORE_DATA && size == sizeof(DWORD),
          label);
    value_len = sizeof(DWORD);
    (void)snprintf(label, sizeof label, "%s hash size", name);
    check(pGetHashParam(prov, hash, HP_HASHSIZE, (LPBYTE)&size, &value_len, 0)
              == TRUE && value_len == sizeof(DWORD)
              && size >= 16u && size <= 64u,
          label);

    value_len = 0;
    (void)snprintf(label, sizeof label, "%s value size query", name);
    check(pGetHashParam(prov, hash, HP_HASHVAL, NULL, &value_len, 0) == FALSE
          && GetLastError() == (DWORD)ERROR_MORE_DATA && value_len == size,
          label);

    value_len = sizeof value;
    (void)snprintf(label, sizeof label, "%s value", name);
    if (pGetHashParam(prov, hash, HP_HASHVAL, value, &value_len, 0) == TRUE) {
        hex_encode(got, value, value_len);
        check(strcmp(got, want) == 0, label);
        if (strcmp(got, want) != 0) {
            printf("  got %s want %s\n", got, want);
        }
    } else {
        check(0, label);
    }

    (void)snprintf(label, sizeof label, "%s duplicate", name);
    check(pDuplicateHash(prov, hash, NULL, 0, &copy) == TRUE && copy != 0,
          label);
    if (copy != 0) {
        value_len = sizeof value;
        (void)snprintf(label, sizeof label, "%s duplicate value", name);
        if (pGetHashParam(prov, copy, HP_HASHVAL, value, &value_len, 0)
                == TRUE) {
            hex_encode(got, value, value_len);
            check(strcmp(got, want) == 0, label);
        } else {
            check(0, label);
        }
        (void)pDestroyHash(prov, copy);
    }

    (void)snprintf(label, sizeof label, "%s destroy", name);
    check(pDestroyHash(prov, hash) == TRUE, label);
}

static void check_provider_params(HCRYPTPROV prov)
{
    DWORD len = 0;
    DWORD value = 0;
    char name[64];

    check(pGetProvParam(prov, PP_NAME, NULL, &len, 0) == FALSE
          && GetLastError() == (DWORD)ERROR_MORE_DATA && len > 1u,
          "PP_NAME size query");
    if (len <= sizeof name) {
        check(pGetProvParam(prov, PP_NAME, (LPBYTE)name, &len, 0) == TRUE
              && strcmp(name, "Anemos OpenSSL CSP") == 0, "PP_NAME");
    } else {
        check(0, "PP_NAME");
    }

    len = sizeof value;
    check(pGetProvParam(prov, PP_PROVTYPE, (LPBYTE)&value, &len, 0) == TRUE
          && value == PROV_RSA_FULL, "PP_PROVTYPE");
    len = sizeof value;
    check(pGetProvParam(prov, PP_VERSION, (LPBYTE)&value, &len, 0) == TRUE
          && value == 0x00000200u, "PP_VERSION");
    len = sizeof value;
    check(pGetProvParam(prov, PP_IMPTYPE, (LPBYTE)&value, &len, 0) == TRUE
          && value == CRYPT_IMPL_SOFTWARE, "PP_IMPTYPE");

    {
        static const ALG_ID want[3] = { CALG_MD5, CALG_SHA1, CALG_SHA_256 };
        static const char *names[3] = { "MD5", "SHA1", "SHA256" };
        unsigned seen = 0;

        for (unsigned i = 0; i < 3; i++) {
            PROV_ENUMALGS entry;
            DWORD entry_len = sizeof entry;
            DWORD flags = (i == 0) ? CRYPT_FIRST : CRYPT_NEXT;

            memset(&entry, 0, sizeof entry);
            if (pGetProvParam(prov, PP_ENUMALGS, (LPBYTE)&entry, &entry_len,
                              flags) == TRUE
                && entry.aiAlgid == want[i]
                && strcmp(entry.szName, names[i]) == 0) {
                seen++;
            }
        }
        check(seen == 3, "PP_ENUMALGS lists MD5, SHA1, SHA256");
        {
            PROV_ENUMALGS entry;
            DWORD entry_len = sizeof entry;

            check(pGetProvParam(prov, PP_ENUMALGS, (LPBYTE)&entry, &entry_len,
                                CRYPT_NEXT) == FALSE
                  && GetLastError() == (DWORD)NTE_NO_MORE_ITEMS,
                  "PP_ENUMALGS ends");
        }
    }
}

/* ---- key objects and AES ---- */

static bool import_plain(HCRYPTPROV prov, ALG_ID alg, const BYTE *key,
                         DWORD key_len, DWORD flags, HCRYPTKEY *out)
{
    BYTE blob[sizeof(BLOBHEADER) + sizeof(DWORD) + 32];
    BLOBHEADER header;
    DWORD cb = key_len;

    memset(&header, 0, sizeof header);
    header.bType = PLAINTEXTKEYBLOB;
    header.bVersion = CUR_BLOB_VERSION;
    header.aiKeyAlg = alg;
    memcpy(blob, &header, sizeof header);
    memcpy(blob + sizeof header, &cb, sizeof cb);
    memcpy(blob + sizeof header + sizeof cb, key, key_len);
    return pImportKey(prov, blob, (DWORD)(sizeof header + sizeof cb + key_len),
                      0, flags, out) == TRUE;
}

static bool export_plain(HCRYPTPROV prov, HCRYPTKEY key, BYTE *blob,
                         DWORD *blob_len)
{
    DWORD len = 0;

    if (pExportKey(prov, key, 0, PLAINTEXTKEYBLOB, 0, NULL, &len) != FALSE
        || GetLastError() != (DWORD)ERROR_MORE_DATA || len > 64u) {
        return false;
    }
    if (pExportKey(prov, key, 0, PLAINTEXTKEYBLOB, 0, blob, &len) != TRUE) {
        return false;
    }
    *blob_len = len;
    return true;
}

static bool set_param_u32(HCRYPTPROV prov, HCRYPTKEY key, DWORD param,
                          DWORD value)
{
    return pSetKeyParam(prov, key, param, (const BYTE *)&value, 0) == TRUE;
}

static bool set_iv(HCRYPTPROV prov, HCRYPTKEY key, const BYTE iv[16])
{
    return pSetKeyParam(prov, key, KP_IV, iv, 0) == TRUE;
}

static bool encrypt_block(HCRYPTPROV prov, HCRYPTKEY key, const BYTE *plain,
                          DWORD plain_len, BYTE *buf, DWORD *len,
                          DWORD capacity)
{
    memcpy(buf, plain, plain_len);
    *len = plain_len;
    return pEncrypt(prov, key, 0, TRUE, 0, buf, len, capacity) == TRUE;
}

static bool decrypt_block(HCRYPTPROV prov, HCRYPTKEY key, const BYTE *cipher,
                          DWORD cipher_len, BYTE *buf, DWORD *len)
{
    memcpy(buf, cipher, cipher_len);
    *len = cipher_len;
    return pDecrypt(prov, key, 0, TRUE, 0, buf, len) == TRUE;
}

static void check_keys(HCRYPTPROV prov)
{
    static const BYTE k128[16] = {
        0x2b, 0x7e, 0x15, 0x16, 0x28, 0xae, 0xd2, 0xa6,
        0xab, 0xf7, 0x15, 0x88, 0x09, 0xcf, 0x4f, 0x3c
    };
    static const BYTE k192[24] = {
        0x8e, 0x73, 0xb0, 0xf7, 0xda, 0x0e, 0x64, 0x52,
        0xc8, 0x10, 0xf3, 0x2b, 0x80, 0x90, 0x79, 0xe5,
        0x62, 0xf8, 0xea, 0xd2, 0x52, 0x2c, 0x6b, 0x7b
    };
    static const BYTE k256[32] = {
        0x60, 0x3d, 0xeb, 0x10, 0x15, 0xca, 0x71, 0xbe,
        0x2b, 0x73, 0xae, 0xf0, 0x85, 0x7d, 0x77, 0x81,
        0x1f, 0x35, 0x2c, 0x07, 0x3b, 0x61, 0x08, 0xd7,
        0x2d, 0x98, 0x10, 0xa3, 0x09, 0x14, 0xdf, 0xf4
    };
    static const BYTE iv[16] = {
        0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
        0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f
    };
    static const BYTE zero_iv[16] = { 0 };
    static const BYTE pt[16] = {
        0x6b, 0xc1, 0xbe, 0xe2, 0x2e, 0x40, 0x9f, 0x96,
        0xe9, 0x3d, 0x7e, 0x11, 0x73, 0x93, 0x17, 0x2a
    };
    static const BYTE ct128cbc[16] = {
        0x76, 0x49, 0xab, 0xac, 0x81, 0x19, 0xb2, 0x46,
        0xce, 0xe9, 0x8e, 0x9b, 0x12, 0xe9, 0x19, 0x7d
    };
    static const BYTE ct128ecb[16] = {
        0x3a, 0xd7, 0x7b, 0xb4, 0x0d, 0x7a, 0x36, 0x60,
        0xa8, 0x9e, 0xca, 0xf3, 0x24, 0x66, 0xef, 0x97
    };
    static const BYTE ct192cbc[16] = {
        0x4f, 0x02, 0x1d, 0xb2, 0x43, 0xbc, 0x63, 0x3d,
        0x71, 0x78, 0x18, 0x3a, 0x9f, 0xa0, 0x71, 0xe8
    };
    static const BYTE ct256cbc[16] = {
        0xf5, 0x8c, 0x4c, 0x04, 0xd6, 0xe5, 0xf1, 0xba,
        0x77, 0x9e, 0xab, 0xfb, 0x5f, 0x7b, 0xfb, 0xd6
    };
    static const BYTE sha256_abc[32] = {
        0xba, 0x78, 0x16, 0xbf, 0x8f, 0x01, 0xcf, 0xea,
        0x41, 0x41, 0x40, 0xde, 0x5d, 0xae, 0x22, 0x23,
        0xb0, 0x03, 0x61, 0xa3, 0x96, 0x17, 0x7a, 0x9c,
        0xb4, 0x10, 0xff, 0x61, 0xf2, 0x00, 0x15, 0xad
    };
    HCRYPTKEY key = 0;
    HCRYPTKEY key2 = 0;
    HCRYPTKEY dup = 0;
    HCRYPTKEY key192 = 0;
    HCRYPTKEY key256 = 0;
    HCRYPTKEY gen = 0;
    HCRYPTKEY derived = 0;
    HCRYPTKEY noexp = 0;
    HCRYPTHASH hash = 0;
    BYTE buf[64];
    BYTE plain[64];
    BYTE blob1[64];
    BYTE blob2[64];
    BYTE iv_out[16];
    BLOBHEADER header;
    DWORD len = 0;
    DWORD len1 = 0;
    DWORD len2 = 0;
    DWORD value = 0;
    DWORD vlen;

    check(import_plain(prov, CALG_AES_128, k128, 16, CRYPT_EXPORTABLE, &key),
          "AES-128 plaintext-key import");
    vlen = sizeof value;
    check(pGetKeyParam(prov, key, KP_ALGID, (LPBYTE)&value, &vlen, 0) == TRUE
          && value == CALG_AES_128, "KP_ALGID");
    vlen = sizeof value;
    check(pGetKeyParam(prov, key, KP_KEYLEN, (LPBYTE)&value, &vlen, 0) == TRUE
          && value == 128u, "KP_KEYLEN");
    vlen = sizeof value;
    check(pGetKeyParam(prov, key, KP_BLOCKLEN, (LPBYTE)&value, &vlen, 0) == TRUE
          && value == 128u, "KP_BLOCKLEN");
    vlen = sizeof value;
    check(pGetKeyParam(prov, key, KP_MODE, (LPBYTE)&value, &vlen, 0) == TRUE
          && value == CRYPT_MODE_CBC, "KP_MODE defaults to CBC");
    vlen = sizeof value;
    check(pGetKeyParam(prov, key, KP_PADDING, (LPBYTE)&value, &vlen, 0) == TRUE
          && value == PKCS5_PADDING, "KP_PADDING defaults to PKCS5");
    vlen = sizeof iv_out;
    check(pGetKeyParam(prov, key, KP_IV, iv_out, &vlen, 0) == TRUE
          && vlen == sizeof iv_out && memcmp(iv_out, zero_iv, 16) == 0,
          "KP_IV defaults to zero");

    check(set_param_u32(prov, key, KP_MODE, CRYPT_MODE_CBC), "set CBC mode");
    check(set_iv(prov, key, iv), "set IV");
    check(set_param_u32(prov, key, KP_PADDING, ZERO_PADDING), "set zero padding");
    check(encrypt_block(prov, key, pt, 16, buf, &len, sizeof buf)
          && len == 16u && memcmp(buf, ct128cbc, 16) == 0,
          "AES-128-CBC vector (SP 800-38A F.2.1)");
    check(decrypt_block(prov, key, buf, len, plain, &len)
          && len == 16u && memcmp(plain, pt, 16) == 0,
          "AES-128-CBC decrypt");

    check(set_param_u32(prov, key, KP_MODE, CRYPT_MODE_ECB), "set ECB mode");
    check(encrypt_block(prov, key, pt, 16, buf, &len, sizeof buf)
          && len == 16u && memcmp(buf, ct128ecb, 16) == 0,
          "AES-128-ECB vector (SP 800-38A F.2.1)");
    check(decrypt_block(prov, key, buf, len, plain, &len)
          && len == 16u && memcmp(plain, pt, 16) == 0,
          "AES-128-ECB decrypt");

    check(set_param_u32(prov, key, KP_MODE, CRYPT_MODE_CBC)
          && set_param_u32(prov, key, KP_PADDING, PKCS5_PADDING),
          "restore CBC + PKCS5");
    check(encrypt_block(prov, key, pt, 16, buf, &len, sizeof buf)
          && len == 32u, "PKCS5 adds a padding block");
    check(decrypt_block(prov, key, buf, len, plain, &len)
          && len == 16u && memcmp(plain, pt, 16) == 0, "PKCS5 round-trip");

    memcpy(buf, pt, 16);
    len = 16;
    check(pEncrypt(prov, key, 0, TRUE, 0, buf, &len, 16) == FALSE
          && GetLastError() == (DWORD)ERROR_MORE_DATA && len == 32u,
          "short encrypt buffer reports the required size");

    check(export_plain(prov, key, blob1, &len1), "export PLAINTEXTKEYBLOB");
    memcpy(&header, blob1, sizeof header);
    memcpy(&value, blob1 + sizeof header, sizeof value);
    check(len1 == (DWORD)(sizeof header + sizeof value + 16)
          && header.bType == PLAINTEXTKEYBLOB
          && header.bVersion == CUR_BLOB_VERSION
          && header.aiKeyAlg == CALG_AES_128 && value == 16u,
          "PLAINTEXTKEYBLOB layout");
    check(pImportKey(prov, blob1, len1, 0, CRYPT_EXPORTABLE, &key2) == TRUE,
          "re-import PLAINTEXTKEYBLOB");
    check(export_plain(prov, key2, blob2, &len2)
          && len2 == len1 && memcmp(blob1, blob2, len1) == 0,
          "export/import round-trip");

    check(pDuplicateKey(prov, key2, NULL, 0, &dup) == TRUE, "duplicate key");
    check(pDestroyKey(prov, key2) == TRUE, "destroy original key");
    check(export_plain(prov, dup, blob2, &len2)
          && len2 == len1 && memcmp(blob1, blob2, len1) == 0,
          "duplicate survives the original");
    check(pDestroyKey(prov, dup) == TRUE, "destroy duplicate key");

    check(import_plain(prov, CALG_AES_192, k192, 24, CRYPT_EXPORTABLE, &key192),
          "AES-192 import");
    check(set_param_u32(prov, key192, KP_MODE, CRYPT_MODE_CBC)
          && set_iv(prov, key192, iv)
          && set_param_u32(prov, key192, KP_PADDING, ZERO_PADDING),
          "AES-192 params");
    check(encrypt_block(prov, key192, pt, 16, buf, &len, sizeof buf)
          && len == 16u && memcmp(buf, ct192cbc, 16) == 0,
          "AES-192-CBC vector (SP 800-38A F.2.3)");
    check(decrypt_block(prov, key192, buf, len, plain, &len)
          && len == 16u && memcmp(plain, pt, 16) == 0,
          "AES-192-CBC decrypt");
    check(pDestroyKey(prov, key192) == TRUE, "destroy AES-192 key");

    check(import_plain(prov, CALG_AES_256, k256, 32, CRYPT_EXPORTABLE, &key256),
          "AES-256 import");
    check(set_param_u32(prov, key256, KP_MODE, CRYPT_MODE_CBC)
          && set_iv(prov, key256, iv)
          && set_param_u32(prov, key256, KP_PADDING, ZERO_PADDING),
          "AES-256 params");
    check(encrypt_block(prov, key256, pt, 16, buf, &len, sizeof buf)
          && len == 16u && memcmp(buf, ct256cbc, 16) == 0,
          "AES-256-CBC vector (SP 800-38A F.2.5)");
    check(decrypt_block(prov, key256, buf, len, plain, &len)
          && len == 16u && memcmp(plain, pt, 16) == 0,
          "AES-256-CBC decrypt");
    check(pDestroyKey(prov, key256) == TRUE, "destroy AES-256 key");

    check(pGenKey(prov, CALG_AES_256, CRYPT_EXPORTABLE, &gen) == TRUE,
          "CPGenKey AES-256");
    check(set_iv(prov, gen, iv)
          && set_param_u32(prov, gen, KP_PADDING, PKCS5_PADDING),
          "generated key params");
    check(encrypt_block(prov, gen, pt, 16, buf, &len, sizeof buf)
          && len == 32u, "generated key encrypt");
    check(decrypt_block(prov, gen, buf, len, plain, &len)
          && len == 16u && memcmp(plain, pt, 16) == 0,
          "generated key round-trip");
    check(export_plain(prov, gen, blob1, &len1), "generated key export");
    check(pDestroyKey(prov, gen) == TRUE, "destroy generated key");

    check(pCreateHash(prov, CALG_SHA_256, 0, 0, &hash) == TRUE
          && pHashData(prov, hash, (const BYTE *)"abc", 3, 0) == TRUE,
          "derive: SHA-256 hash");
    check(pDeriveKey(prov, CALG_AES_256, hash, CRYPT_EXPORTABLE, &derived)
          == TRUE, "CPDeriveKey AES-256");
    check(export_plain(prov, derived, blob1, &len1), "derived key export");
    memcpy(&value, blob1 + sizeof header, sizeof value);
    check(len1 == (DWORD)(sizeof header + sizeof value + 32)
          && value == 32u
          && memcmp(blob1 + sizeof header + sizeof value, sha256_abc, 32) == 0,
          "derived key is the SHA-256 prefix");
    check(pDestroyHash(prov, hash) == TRUE, "destroy derive hash");
    check(pDestroyKey(prov, derived) == TRUE, "destroy derived key");

    check(pGenKey(prov, (ALG_ID)0x1234, 0, &key2) == FALSE
          && GetLastError() == (DWORD)NTE_BAD_ALGID,
          "unsupported key algorithm");
    check(import_plain(prov, CALG_AES_128, k128, 16, 0, &noexp) == TRUE,
          "non-exportable import");
    len1 = sizeof blob1;
    check(pExportKey(prov, noexp, 0, PLAINTEXTKEYBLOB, 0, blob1, &len1) == FALSE
          && GetLastError() == (DWORD)NTE_PERM,
          "non-exportable key refuses export");
    check(set_param_u32(prov, noexp, KP_MODE, 99) == FALSE,
          "invalid mode is rejected");
    check(pDestroyKey(prov, noexp) == TRUE, "destroy non-exportable key");

    {
        BYTE bad[sizeof(BLOBHEADER) + sizeof(DWORD) + 16];
        DWORD bad_cb = 15;

        memset(&header, 0, sizeof header);
        header.bType = PLAINTEXTKEYBLOB;
        header.bVersion = CUR_BLOB_VERSION;
        header.aiKeyAlg = CALG_AES_128;
        memcpy(bad, &header, sizeof header);
        memcpy(bad + sizeof header, &bad_cb, sizeof bad_cb);
        memcpy(bad + sizeof header + sizeof bad_cb, k128, 15);
        check(pImportKey(prov, bad, sizeof bad, 0, 0, &key2) == FALSE,
              "wrong key length is rejected");
    }

    /* `key` still has CBC + PKCS5 from above. */
    check(encrypt_block(prov, key, pt, 16, buf, &len, sizeof buf)
          && len == 32u, "tamper setup");
    buf[31] ^= 0x01u;
    len = 32;
    check(pDecrypt(prov, key, 0, TRUE, 0, buf, &len) == FALSE,
          "tampered PKCS5 ciphertext is rejected");
    check(pDestroyKey(prov, key) == TRUE, "destroy AES-128 key");
}

static DWORD encrypt_chunk(HCRYPTPROV prov, HCRYPTKEY key, const BYTE *in,
                           DWORD in_len, BOOL final, BYTE *out, DWORD cap,
                           bool *ok)
{
    DWORD len = in_len;

    memcpy(out, in, in_len);
    *ok = pEncrypt(prov, key, 0, final, 0, out, &len, cap) == TRUE;
    return len;
}

static DWORD decrypt_chunk(HCRYPTPROV prov, HCRYPTKEY key, const BYTE *in,
                           DWORD in_len, BOOL final, BYTE *out, bool *ok)
{
    DWORD len = in_len;

    memcpy(out, in, in_len);
    *ok = pDecrypt(prov, key, 0, final, 0, out, &len) == TRUE;
    return len;
}

static void check_streaming(HCRYPTPROV prov)
{
    static const BYTE key_bytes[16] = {
        0x2b, 0x7e, 0x15, 0x16, 0x28, 0xae, 0xd2, 0xa6,
        0xab, 0xf7, 0x15, 0x88, 0x09, 0xcf, 0x4f, 0x3c
    };
    static const BYTE iv[16] = {
        0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
        0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f
    };
    static const BYTE pt[32] = {
        0x6b, 0xc1, 0xbe, 0xe2, 0x2e, 0x40, 0x9f, 0x96,
        0xe9, 0x3d, 0x7e, 0x11, 0x73, 0x93, 0x17, 0x2a,
        0xae, 0x2d, 0x8a, 0x57, 0x1e, 0x03, 0xac, 0x9c,
        0x9e, 0xb7, 0x6f, 0xac, 0x45, 0xaf, 0x8e, 0x51
    };
    static const BYTE ct[32] = {
        0x76, 0x49, 0xab, 0xac, 0x81, 0x19, 0xb2, 0x46,
        0xce, 0xe9, 0x8e, 0x9b, 0x12, 0xe9, 0x19, 0x7d,
        0x50, 0x86, 0xcb, 0x9b, 0x50, 0x72, 0x19, 0xee,
        0x95, 0xdb, 0x11, 0x3a, 0x91, 0x76, 0x78, 0xb2
    };
    HCRYPTKEY key = 0;
    BYTE chunk[64];
    BYTE got[64];
    BYTE plain[64];
    BYTE first[16];
    BYTE second[16];
    DWORD len;
    DWORD total;
    bool ok;

    check(import_plain(prov, CALG_AES_128, key_bytes, 16, CRYPT_EXPORTABLE,
                       &key), "streaming key import");
    check(set_param_u32(prov, key, KP_MODE, CRYPT_MODE_CBC)
          && set_iv(prov, key, iv)
          && set_param_u32(prov, key, KP_PADDING, ZERO_PADDING),
          "streaming params");

    total = 0;
    len = encrypt_chunk(prov, key, pt, 5, FALSE, chunk, sizeof chunk, &ok);
    check(ok && len == 0u, "stream: 5 bytes buffered, no output");
    len = encrypt_chunk(prov, key, pt + 5, 11, FALSE, chunk, sizeof chunk, &ok);
    check(ok && len == 16u, "stream: 11 more emits the first block");
    memcpy(got + total, chunk, len);
    total += len;
    len = encrypt_chunk(prov, key, pt + 16, 16, TRUE, chunk, sizeof chunk, &ok);
    check(ok && len == 16u, "stream: final emits the second block");
    memcpy(got + total, chunk, len);
    total += len;
    check(total == 32u && memcmp(got, ct, 32) == 0,
          "streaming ciphertext matches SP 800-38A");

    total = 0;
    len = decrypt_chunk(prov, key, ct, 7, FALSE, chunk, &ok);
    check(ok && len == 0u, "decrypt stream: 7 bytes buffered");
    len = decrypt_chunk(prov, key, ct + 7, 9, FALSE, chunk, &ok);
    check(ok && len == 16u, "decrypt stream: 16 bytes emits plaintext");
    memcpy(plain + total, chunk, len);
    total += len;
    len = decrypt_chunk(prov, key, ct + 16, 16, TRUE, chunk, &ok);
    check(ok && len == 16u, "decrypt stream: final block");
    memcpy(plain + total, chunk, len);
    total += len;
    check(total == 32u && memcmp(plain, pt, 32) == 0,
          "streaming decrypt matches SP 800-38A");

    check(set_param_u32(prov, key, KP_PADDING, PKCS5_PADDING)
          && set_iv(prov, key, iv), "PKCS5 stream reset");
    total = 0;
    len = encrypt_chunk(prov, key, pt, 5, FALSE, chunk, sizeof chunk, &ok);
    check(ok && len == 0u, "PKCS5 stream: 5 bytes buffered");
    len = encrypt_chunk(prov, key, pt + 5, 15, TRUE, chunk, sizeof chunk, &ok);
    check(ok && len == 32u, "PKCS5 stream: final pads to two blocks");
    memcpy(got, chunk, len);
    total = 0;
    len = decrypt_chunk(prov, key, got, 16, FALSE, chunk, &ok);
    check(ok && len <= 16u, "PKCS5 decrypt stream: first call buffers");
    memcpy(plain + total, chunk, len);
    total += len;
    len = decrypt_chunk(prov, key, got + 16, 16, TRUE, chunk, &ok);
    check(ok && len <= 20u, "PKCS5 decrypt stream: final completes");
    memcpy(plain + total, chunk, len);
    total += len;
    check(total == 20u && memcmp(plain, pt, 20) == 0,
          "PKCS5 streaming round-trip");

    check(set_param_u32(prov, key, KP_PADDING, ZERO_PADDING)
          && set_iv(prov, key, iv), "zero-padding stream reset");
    len = 5;
    memcpy(chunk, pt, 5);
    check(pEncrypt(prov, key, 0, TRUE, 0, chunk, &len, sizeof chunk) == FALSE,
          "zero padding rejects a partial final block");

    memcpy(first, pt, 16);
    len = 16;
    check(pEncrypt(prov, key, 0, TRUE, 0, first, &len, sizeof first) == TRUE
          && len == 16u, "one-shot after stream resets");
    memcpy(second, pt, 16);
    len = 16;
    check(pEncrypt(prov, key, 0, TRUE, 0, second, &len, sizeof second) == TRUE
          && len == 16u, "second one-shot");
    check(memcmp(first, second, 16) == 0,
          "each final operation restarts from the configured IV");

    check(pDestroyKey(prov, key) == TRUE, "destroy streaming key");
}

static void check_rsa(HCRYPTPROV prov)
{
    HCRYPTKEY user = 0;
    HCRYPTKEY user2 = 0;
    HCRYPTKEY pub = 0;
    HCRYPTKEY rsa = 0;
    HCRYPTKEY priv2 = 0;
    HCRYPTKEY noexp = 0;
    HCRYPTHASH hash = 0;
    HCRYPTHASH hash2 = 0;
    BYTE pubblob[512];
    BYTE privblob[1024];
    BYTE pub2[512];
    BYTE sig[256];
    DWORD publen = 0;
    DWORD privlen = 0;
    DWORD siglen = 0;
    DWORD vlen;
    DWORD value;
    BLOBHEADER header;
    RSAPUBKEY rsa_header;

    check(pGetUserKey(prov, AT_SIGNATURE, &user) == TRUE && user != 0,
          "CPGetUserKey AT_SIGNATURE");
    check(pGetUserKey(prov, AT_SIGNATURE, &user2) == TRUE && user2 == user,
          "CPGetUserKey is stable");
    check(pGetUserKey(prov, 99, &user2) == FALSE, "invalid key spec rejected");
    check(pGetUserKey(prov, AT_KEYEXCHANGE, &user2) == TRUE && user2 != 0,
          "CPGetUserKey AT_KEYEXCHANGE");

    publen = 0;
    check(pExportKey(prov, user, 0, PUBLICKEYBLOB, 0, NULL, &publen) == FALSE
          && GetLastError() == (DWORD)ERROR_MORE_DATA && publen == 148u,
          "RSA public blob size query");
    publen = sizeof pubblob;
    check(pExportKey(prov, user, 0, PUBLICKEYBLOB, 0, pubblob, &publen) == TRUE
          && publen == 148u, "RSA public blob export");
    memcpy(&header, pubblob, sizeof header);
    memcpy(&rsa_header, pubblob + sizeof header, sizeof rsa_header);
    check(header.bType == PUBLICKEYBLOB && header.bVersion == CUR_BLOB_VERSION
          && header.aiKeyAlg == CALG_RSA_SIGN
          && rsa_header.magic == 0x31415352u && rsa_header.bitlen == 1024u
          && rsa_header.pubexp == 65537u, "RSA public blob layout");
    check(pImportKey(prov, pubblob, publen, 0, 0, &pub) == TRUE && pub != 0,
          "RSA public blob import");
    vlen = sizeof value;
    check(pGetKeyParam(prov, pub, KP_KEYLEN, (LPBYTE)&value, &vlen, 0) == TRUE
          && value == 1024u, "RSA KP_KEYLEN");
    vlen = sizeof value;
    check(pGetKeyParam(prov, pub, KP_ALGID, (LPBYTE)&value, &vlen, 0) == TRUE
          && value == CALG_RSA_SIGN, "RSA KP_ALGID");

    check(pCreateHash(prov, CALG_SHA_256, 0, 0, &hash) == TRUE
          && pHashData(prov, hash, (const BYTE *)"abc", 3, 0) == TRUE,
          "RSA sign hash");
    siglen = 0;
    check(pSignHash(prov, hash, AT_SIGNATURE, NULL, 0, NULL, &siglen) == FALSE
          && GetLastError() == (DWORD)ERROR_MORE_DATA && siglen == 128u,
          "RSA signature size query");
    siglen = sizeof sig;
    check(pSignHash(prov, hash, AT_SIGNATURE, NULL, 0, sig, &siglen) == TRUE
          && siglen == 128u, "CPSignHash SHA-256");
    check(pVerifySignature(prov, hash, sig, siglen, pub, NULL, 0) == TRUE,
          "CPVerifySignature accepts the signature");
    sig[0] ^= 0x01u;
    check(pVerifySignature(prov, hash, sig, siglen, pub, NULL, 0) == FALSE,
          "tampered signature is rejected");
    sig[0] ^= 0x01u;
    check(pVerifySignature(prov, hash, sig, siglen, user, NULL, 0) == TRUE,
          "verify with the private handle");

    check(pCreateHash(prov, CALG_SHA_256, 0, 0, &hash2) == TRUE
          && pHashData(prov, hash2, (const BYTE *)"abd", 3, 0) == TRUE,
          "RSA wrong-digest hash");
    check(pVerifySignature(prov, hash2, sig, siglen, pub, NULL, 0) == FALSE,
          "signature does not verify a different digest");

    check(pGenKey(prov, CALG_RSA_KEYX, CRYPT_EXPORTABLE, &rsa) == TRUE,
          "CPGenKey RSA_KEYX");
    privlen = 0;
    check(pExportKey(prov, rsa, 0, PRIVATEKEYBLOB, 0, NULL, &privlen) == FALSE
          && GetLastError() == (DWORD)ERROR_MORE_DATA && privlen == 596u,
          "RSA private blob size query");
    privlen = sizeof privblob;
    check(pExportKey(prov, rsa, 0, PRIVATEKEYBLOB, 0, privblob, &privlen) == TRUE
          && privlen == 596u, "RSA private blob export");
    memcpy(&header, privblob, sizeof header);
    memcpy(&rsa_header, privblob + sizeof header, sizeof rsa_header);
    check(header.bType == PRIVATEKEYBLOB && header.aiKeyAlg == CALG_RSA_KEYX
          && rsa_header.magic == 0x32415352u && rsa_header.bitlen == 1024u,
          "RSA private blob layout");
    check(pImportKey(prov, privblob, privlen, 0, CRYPT_EXPORTABLE, &priv2)
          == TRUE, "RSA private blob import");
    {
        DWORD left = sizeof pubblob;
        DWORD right = sizeof pub2;

        check(pExportKey(prov, rsa, 0, PUBLICKEYBLOB, 0, pubblob, &left)
              == TRUE, "export public from original RSA key");
        check(pExportKey(prov, priv2, 0, PUBLICKEYBLOB, 0, pub2, &right)
              == TRUE, "export public from imported private key");
        check(left == right && memcmp(pubblob, pub2, left) == 0,
              "private import preserves the key");
    }

    check(pGenKey(prov, CALG_RSA_SIGN, 0, &noexp) == TRUE,
          "non-exportable RSA key");
    privlen = 0;
    check(pExportKey(prov, noexp, 0, PRIVATEKEYBLOB, 0, NULL, &privlen) == FALSE
          && GetLastError() == (DWORD)NTE_PERM,
          "non-exportable private key refuses export");

    check(pDestroyKey(prov, noexp) == TRUE, "destroy non-exportable RSA key");
    check(pDestroyKey(prov, priv2) == TRUE, "destroy imported RSA key");
    check(pDestroyKey(prov, rsa) == TRUE, "destroy generated RSA key");
    check(pDestroyKey(prov, pub) == TRUE, "destroy public RSA key");
    check(pDestroyKey(prov, user) == TRUE, "destroy user key");
    check(pDestroyHash(prov, hash2) == TRUE, "destroy wrong-digest hash");
    check(pDestroyHash(prov, hash) == TRUE, "destroy sign hash");
}

static void check_stubs(HCRYPTPROV prov, HCRYPTHASH hash)
{
    HCRYPTKEY key = 0;
    BYTE sig[8];
    DWORD sig_len = sizeof sig;

    check(pGenKey(prov, (ALG_ID)0x1234, 0, &key) == FALSE
          && GetLastError() == (DWORD)NTE_BAD_ALGID,
          "unsupported key algorithm");
    check(pSignHash(prov, hash, 99, NULL, 0, sig, &sig_len) == FALSE
          && GetLastError() == (DWORD)NTE_BAD_KEY,
          "invalid signing key spec");
    check(pCreateHash(prov, (ALG_ID)0x1234, 0, 0, &hash) == FALSE
          && GetLastError() == (DWORD)NTE_BAD_ALGID,
          "unknown hash algorithm is rejected");
    check(pHashData(prov, (HCRYPTHASH)0x12345, (const BYTE *)"x", 1, 0)
              == FALSE && GetLastError() == (DWORD)NTE_BAD_HASH,
          "invalid hash handle is rejected");
    check(pDestroyHash(prov, (HCRYPTHASH)0x12345) == FALSE,
          "destroying an invalid hash fails");
    check(pGenRandom(prov, 16, NULL) == FALSE, "NULL random buffer is rejected");
}

#ifdef ANEM_CSP_NEGATIVE
#define KAT_SHA1 "0000000000000000000000000000000000000000"
#else
#define KAT_SHA1 "a9993e364706816aba3e25717850c26c9cd0d89d"
#endif

int main(int argc, char **argv)
{
    const char *dll_path = (argc > 1) ? argv[1] : "anemosp.dll";
    HMODULE dll;
    HCRYPTPROV prov = 0;
    HCRYPTPROV prov2 = 0;
    AnemVTableProvStruc vtable;
    BYTE rnd1[32];
    BYTE rnd2[32];

    if (argc > 2 && argv[2][0] != '\0') {
        record = fopen(argv[2], "w");
        if (record == NULL) {
            printf("FAIL cannot open %s\n", argv[2]);
            return 1;
        }
        setvbuf(record, NULL, _IONBF, 0);
    }

    dll = LoadLibraryA(dll_path);
    if (dll == NULL) {
        printf("FAIL cannot load %s (error %lu)\n", dll_path,
               (unsigned long)GetLastError());
        if (record != NULL) {
            fprintf(record, "FAIL cannot load %s\n", dll_path);
            fclose(record);
        }
        return 1;
    }
    printf("loaded %s\n", dll_path);
    if (record != NULL) {
        fprintf(record, "loaded %s\n", dll_path);
    }
    check_exports(dll);

    pAcquire = (FnAcquire)(void *)GetProcAddress(dll, "CPAcquireContext");
    pRelease = (FnRelease)(void *)GetProcAddress(dll, "CPReleaseContext");
    pGenRandom = (FnGenRandom)(void *)GetProcAddress(dll, "CPGenRandom");
    pCreateHash = (FnCreateHash)(void *)GetProcAddress(dll, "CPCreateHash");
    pHashData = (FnHashData)(void *)GetProcAddress(dll, "CPHashData");
    pGetHashParam = (FnGetHashParam)(void *)GetProcAddress(dll,
                                                          "CPGetHashParam");
    pDestroyHash = (FnDestroyHash)(void *)GetProcAddress(dll, "CPDestroyHash");
    pDuplicateHash = (FnDuplicateHash)(void *)GetProcAddress(dll,
                                                            "CPDuplicateHash");
    pGetProvParam = (FnGetProvParam)(void *)GetProcAddress(dll,
                                                          "CPGetProvParam");
    pGenKey = (FnGenKey)(void *)GetProcAddress(dll, "CPGenKey");
    pSignHash = (FnSignHash)(void *)GetProcAddress(dll, "CPSignHash");
    pDeriveKey = (FnDeriveKey)(void *)GetProcAddress(dll, "CPDeriveKey");
    pDestroyKey = (FnDestroyKey)(void *)GetProcAddress(dll, "CPDestroyKey");
    pGetKeyParam = (FnGetKeyParam)(void *)GetProcAddress(dll,
                                                          "CPGetKeyParam");
    pSetKeyParam = (FnSetKeyParam)(void *)GetProcAddress(dll,
                                                          "CPSetKeyParam");
    pImportKey = (FnImportKey)(void *)GetProcAddress(dll, "CPImportKey");
    pExportKey = (FnExportKey)(void *)GetProcAddress(dll, "CPExportKey");
    pEncrypt = (FnEncrypt)(void *)GetProcAddress(dll, "CPEncrypt");
    pDecrypt = (FnDecrypt)(void *)GetProcAddress(dll, "CPDecrypt");
    pDuplicateKey = (FnDuplicateKey)(void *)GetProcAddress(dll,
                                                           "CPDuplicateKey");
    pGetUserKey = (FnGetUserKey)(void *)GetProcAddress(dll, "CPGetUserKey");
    pVerifySignature = (FnVerifySignature)(void *)GetProcAddress(dll,
                                                           "CPVerifySignature");
    if (pAcquire == NULL || pRelease == NULL || pGenRandom == NULL
        || pCreateHash == NULL || pHashData == NULL || pGetHashParam == NULL
        || pDestroyHash == NULL || pDuplicateHash == NULL
        || pGetProvParam == NULL || pGenKey == NULL || pSignHash == NULL
        || pDeriveKey == NULL || pDestroyKey == NULL || pGetKeyParam == NULL
        || pSetKeyParam == NULL || pImportKey == NULL || pExportKey == NULL
        || pEncrypt == NULL || pDecrypt == NULL || pDuplicateKey == NULL
        || pGetUserKey == NULL || pVerifySignature == NULL) {
        printf("FAIL required function pointers\n");
        if (record != NULL) {
            fprintf(record, "FAIL required function pointers\n");
            fclose(record);
        }
        return 1;
    }

    memset(&vtable, 0, sizeof vtable);
    vtable.Version = 3;
    vtable.dwProvType = PROV_RSA_FULL;
    vtable.pszProvName = "Anemos OpenSSL CSP";

    check(pAcquire(&prov, NULL, CRYPT_VERIFYCONTEXT, &vtable) == TRUE
          && prov != 0, "CPAcquireContext");
    check(pAcquire(&prov2, "container", 0, &vtable) == TRUE
          && prov2 != 0 && prov2 != prov, "CPAcquireContext container");
    check(pRelease(prov2, 0) == TRUE, "CPReleaseContext");

    memset(rnd1, 0, sizeof rnd1);
    memset(rnd2, 0, sizeof rnd2);
    check(pGenRandom(prov, sizeof rnd1, rnd1) == TRUE
          && pGenRandom(prov, sizeof rnd2, rnd2) == TRUE
          && memcmp(rnd1, rnd2, sizeof rnd1) != 0,
          "CPGenRandom returns distinct buffers");
    {
        unsigned nonzero = 0;

        for (unsigned i = 0; i < sizeof rnd1; i++) {
            if (rnd1[i] != 0 || rnd2[i] != 0) {
                nonzero++;
            }
        }
        check(nonzero != 0, "CPGenRandom output is not all zero");
    }

    check_hash(prov, CALG_MD5, "MD5", "900150983cd24fb0d6963f7d28e17f72");
    check_hash(prov, CALG_SHA1, "SHA-1", KAT_SHA1);
    check_hash(prov, CALG_SHA_256, "SHA-256",
               "ba7816bf8f01cfea414140de5dae2223"
               "b00361a396177a9cb410ff61f20015ad");
    check_keys(prov);
    check_streaming(prov);
    check_rsa(prov);
    check_provider_params(prov);
    check_stubs(prov, 0);
    check(pRelease(prov, 0) == TRUE, "CPReleaseContext final");

    FreeLibrary(dll);
    if (failures != 0u) {
        printf("FAILED: %u check(s)\n", failures);
        if (record != NULL) {
            fprintf(record, "FAILED: %u check(s)\n", failures);
            fclose(record);
        }
        return 1;
    }
    printf("all CSP SPI checks pass\n");
    if (record != NULL) {
        fprintf(record, "all CSP SPI checks pass\n");
        fflush(record);
        (void)_commit(_fileno(record));
        fclose(record);
        /* Guest mode: stay on screen long enough for the host's pixel probe
         * to see the console. The runner screenshots ~4 s after the Run box
         * and then polls for the console to close; a fast harness would
         * otherwise exit before either observation. */
        Sleep(8000);
        /* Power the guest off from inside: the Run-box text fallback can be
         * lost to a focus thief, and a hard cut risks the certificate. */
        (void)WinExec("shutdown -s -f -t 0", SW_HIDE);
    }
    return 0;
}
