/* CSP SPI harness: load the provider DLL and call it the way CryptoAPI would,
 * but without the provider-signature check that stock XP applies before
 * CryptAcquireContext. This is the P3.1 gate.
 *
 * Checks: every required export resolves; contexts acquire and release;
 * CPGenRandom produces distinct non-zero bytes; MD5/SHA-1/SHA-256 match
 * published answers through CPCreateHash/CPHashData/CPGetHashParam; provider
 * properties answer; unsupported key operations fail explicitly.
 *
 * usage: spi_test.exe anemosp.dll
 */
#include <stdio.h>
#include <string.h>

#include "csp_abi.h"

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

static void check_stubs(HCRYPTPROV prov, HCRYPTHASH hash)
{
    HCRYPTKEY key = 0;
    BYTE sig[8];
    DWORD sig_len = sizeof sig;

    check(pGenKey(prov, CALG_RSA_KEYX, 0, &key) == FALSE
          && GetLastError() == (DWORD)NTE_NOT_SUPPORTED,
          "CPGenKey is an explicit stub");
    check(pSignHash(prov, hash, AT_SIGNATURE, NULL, 0, sig, &sig_len) == FALSE
          && GetLastError() == (DWORD)NTE_NOT_SUPPORTED,
          "CPSignHash is an explicit stub");
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
    HMODULE dll = LoadLibraryA(dll_path);
    HCRYPTPROV prov = 0;
    HCRYPTPROV prov2 = 0;
    AnemVTableProvStruc vtable;
    BYTE rnd1[32];
    BYTE rnd2[32];

    if (dll == NULL) {
        printf("FAIL cannot load %s (error %lu)\n", dll_path,
               (unsigned long)GetLastError());
        return 1;
    }
    printf("loaded %s\n", dll_path);
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
    if (pAcquire == NULL || pRelease == NULL || pGenRandom == NULL
        || pCreateHash == NULL || pHashData == NULL || pGetHashParam == NULL
        || pDestroyHash == NULL || pDuplicateHash == NULL
        || pGetProvParam == NULL || pGenKey == NULL || pSignHash == NULL) {
        printf("FAIL required function pointers\n");
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
    check_provider_params(prov);
    check_stubs(prov, 0);
    check(pRelease(prov, 0) == TRUE, "CPReleaseContext final");

    FreeLibrary(dll);
    if (failures != 0u) {
        printf("FAILED: %u check(s)\n", failures);
        return 1;
    }
    printf("all CSP SPI checks pass\n");
    return 0;
}
