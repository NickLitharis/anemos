/* Anemos CSP — the first SPI slice: contexts, hashing, randomness, provider
 * properties. Key objects and asymmetric operations are explicit stubs that
 * return NTE_NOT_SUPPORTED.
 *
 * Engine: the statically linked OpenSSL 3.5.7 built by
 * anemos/crypto/build-openssl.sh. Randomness comes from SystemFunction036
 * (RtlGenRandom), NOT OpenSSL's Windows RAND source: that one uses CryptoAPI,
 * which would re-enter this DLL when it is installed as rsaenh.dll.
 *
 * This is user-mode code, so the kernel-only rules (§1 allocation, IRQL) do
 * not apply; the bounds and error-propagation rules do.
 */
#define ANEM_CSP_BUILD 1
#include "csp_abi.h"

#include <openssl/evp.h>
#include <stdbool.h>
#include <string.h>

#define ANEM_CSP_SLOTS 32u
#define ANEM_CTX_BASE ((ULONG_PTR)0x00010000u)
#define ANEM_HASH_BASE ((ULONG_PTR)0x00020000u)

typedef struct AnemCspContext {
    bool used;
    DWORD flags;
    DWORD enum_index;
    char container[64];
} AnemCspContext;

typedef struct AnemCspHash {
    bool used;
    HCRYPTPROV prov;
    ALG_ID alg;
    EVP_MD_CTX *md;
} AnemCspHash;

static CRITICAL_SECTION g_lock;
static bool g_ready;
static AnemCspContext g_ctx[ANEM_CSP_SLOTS];
static AnemCspHash g_hash[ANEM_CSP_SLOTS];

/* advapi32's RtlGenRandom. Declared here rather than including a header whose
 * availability differs across SDKs; the symbol is stable. */
BOOLEAN WINAPI SystemFunction036(PVOID pbBuffer, ULONG cbBuffer);

static BOOL fail(DWORD code)
{
    SetLastError(code);
    return FALSE;
}

static AnemCspContext *ctx_lookup(HCRYPTPROV hProv)
{
    ULONG_PTR index;

    if (hProv < ANEM_CTX_BASE || hProv >= ANEM_CTX_BASE + ANEM_CSP_SLOTS) {
        return NULL;
    }
    index = hProv - ANEM_CTX_BASE;
    return g_ctx[index].used ? &g_ctx[index] : NULL;
}

static HCRYPTPROV ctx_handle(const AnemCspContext *ctx)
{
    return (HCRYPTPROV)(ANEM_CTX_BASE + (ULONG_PTR)(ctx - g_ctx));
}

static AnemCspHash *hash_lookup(HCRYPTHASH hHash)
{
    ULONG_PTR index;

    if (hHash < ANEM_HASH_BASE || hHash >= ANEM_HASH_BASE + ANEM_CSP_SLOTS) {
        return NULL;
    }
    index = hHash - ANEM_HASH_BASE;
    return g_hash[index].used ? &g_hash[index] : NULL;
}

static HCRYPTHASH hash_handle(const AnemCspHash *hash)
{
    return (HCRYPTHASH)(ANEM_HASH_BASE + (ULONG_PTR)(hash - g_hash));
}

static void destroy_hashes_of(HCRYPTPROV hProv)
{
    for (unsigned i = 0; i < ANEM_CSP_SLOTS; i++) {
        if (g_hash[i].used && g_hash[i].prov == hProv) {
            EVP_MD_CTX_free(g_hash[i].md);
            g_hash[i].md = NULL;
            g_hash[i].used = false;
        }
    }
}

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID reserved)
{
    (void)reserved;
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(instance);
        InitializeCriticalSection(&g_lock);
        g_ready = true;
    } else if (reason == DLL_PROCESS_DETACH && g_ready) {
        for (unsigned i = 0; i < ANEM_CSP_SLOTS; i++) {
            if (g_hash[i].used) {
                EVP_MD_CTX_free(g_hash[i].md);
                g_hash[i].md = NULL;
                g_hash[i].used = false;
            }
        }
        DeleteCriticalSection(&g_lock);
        g_ready = false;
    }
    return TRUE;
}

static BOOL acquire(HCRYPTPROV *phProv, LPCSTR container, DWORD dwFlags)
{
    AnemCspContext *slot = NULL;

    if (phProv == NULL) {
        return fail(ERROR_INVALID_PARAMETER);
    }
    *phProv = 0;
    if ((dwFlags & CRYPT_DELETEKEYSET) != 0) {
        return fail(NTE_BAD_FLAGS);
    }
    EnterCriticalSection(&g_lock);
    for (unsigned i = 0; i < ANEM_CSP_SLOTS; i++) {
        if (!g_ctx[i].used) {
            slot = &g_ctx[i];
            break;
        }
    }
    if (slot == NULL) {
        LeaveCriticalSection(&g_lock);
        return fail(NTE_NO_MEMORY);
    }
    memset(slot, 0, sizeof *slot);
    slot->used = true;
    slot->flags = dwFlags;
    if (container != NULL) {
        size_t n = 0;

        while (n + 1u < sizeof slot->container && container[n] != '\0') {
            slot->container[n] = container[n];
            n++;
        }
        slot->container[n] = '\0';
    }
    *phProv = ctx_handle(slot);
    LeaveCriticalSection(&g_lock);
    return TRUE;
}

ANEM_CSP_API BOOL WINAPI CPAcquireContext(HCRYPTPROV *phProv,
                                          LPCSTR szContainer, DWORD dwFlags,
                                          AnemVTableProvStruc *pVTable)
{
    (void)pVTable;
    return acquire(phProv, szContainer, dwFlags);
}

ANEM_CSP_API BOOL WINAPI CPAcquireContextW(HCRYPTPROV *phProv,
                                           LPCWSTR szContainer, DWORD dwFlags,
                                           void *pVTable)
{
    (void)szContainer;
    (void)pVTable;
    return acquire(phProv, "wide", dwFlags);
}

ANEM_CSP_API BOOL WINAPI CPReleaseContext(HCRYPTPROV hProv, DWORD dwFlags)
{
    AnemCspContext *ctx;

    (void)dwFlags;
    EnterCriticalSection(&g_lock);
    ctx = ctx_lookup(hProv);
    if (ctx != NULL) {
        destroy_hashes_of(hProv);
        memset(ctx, 0, sizeof *ctx);
    }
    LeaveCriticalSection(&g_lock);
    return ctx != NULL ? TRUE : fail(NTE_BAD_KEYSET);
}

ANEM_CSP_API BOOL WINAPI CPGenRandom(HCRYPTPROV hProv, DWORD cbLen,
                                     LPBYTE pbBuffer)
{
    AnemCspContext *ctx;
    BOOLEAN ok;

    if (cbLen != 0u && pbBuffer == NULL) {
        return fail(ERROR_INVALID_PARAMETER);
    }
    EnterCriticalSection(&g_lock);
    ctx = ctx_lookup(hProv);
    LeaveCriticalSection(&g_lock);
    if (ctx == NULL) {
        return fail(NTE_BAD_KEYSET);
    }
    if (cbLen == 0u) {
        return TRUE;
    }
    ok = SystemFunction036(pbBuffer, (ULONG)cbLen);
    return ok ? TRUE : fail(ERROR_GEN_FAILURE);
}

static const EVP_MD *hash_md(ALG_ID alg)
{
    switch (alg) {
    case CALG_MD5:
        return EVP_md5();
    case CALG_SHA1:
        return EVP_sha1();
    case CALG_SHA_256:
        return EVP_sha256();
    default:
        return NULL;
    }
}

ANEM_CSP_API BOOL WINAPI CPCreateHash(HCRYPTPROV hProv, ALG_ID Algid,
                                      HCRYPTKEY hKey, DWORD dwFlags,
                                      HCRYPTHASH *phHash)
{
    const EVP_MD *md;
    AnemCspHash *slot = NULL;
    EVP_MD_CTX *md_ctx;

    (void)dwFlags;
    if (phHash == NULL) {
        return fail(ERROR_INVALID_PARAMETER);
    }
    *phHash = 0;
    if (hKey != 0) {
        return fail(NTE_NOT_SUPPORTED); /* keyed hashes are a later slice */
    }
    md = hash_md(Algid);
    if (md == NULL) {
        return fail(NTE_BAD_ALGID);
    }
    md_ctx = EVP_MD_CTX_new();
    if (md_ctx == NULL) {
        return fail(NTE_NO_MEMORY);
    }
    if (EVP_DigestInit_ex(md_ctx, md, NULL) != 1) {
        EVP_MD_CTX_free(md_ctx);
        return fail(NTE_FAIL);
    }
    EnterCriticalSection(&g_lock);
    if (ctx_lookup(hProv) == NULL) {
        LeaveCriticalSection(&g_lock);
        EVP_MD_CTX_free(md_ctx);
        return fail(NTE_BAD_KEYSET);
    }
    for (unsigned i = 0; i < ANEM_CSP_SLOTS; i++) {
        if (!g_hash[i].used) {
            slot = &g_hash[i];
            break;
        }
    }
    if (slot == NULL) {
        LeaveCriticalSection(&g_lock);
        EVP_MD_CTX_free(md_ctx);
        return fail(NTE_NO_MEMORY);
    }
    memset(slot, 0, sizeof *slot);
    slot->used = true;
    slot->prov = hProv;
    slot->alg = Algid;
    slot->md = md_ctx;
    *phHash = hash_handle(slot);
    LeaveCriticalSection(&g_lock);
    return TRUE;
}

ANEM_CSP_API BOOL WINAPI CPHashData(HCRYPTPROV hProv, HCRYPTHASH hHash,
                                    const BYTE *pbData, DWORD cbDataLen,
                                    DWORD dwFlags)
{
    AnemCspHash *hash;
    BOOL ok = FALSE;
    bool found = false;

    (void)dwFlags;
    if (cbDataLen != 0u && pbData == NULL) {
        return fail(ERROR_INVALID_PARAMETER);
    }
    EnterCriticalSection(&g_lock);
    hash = hash_lookup(hHash);
    if (hash != NULL && hash->prov == hProv && hash->md != NULL) {
        found = true;
        ok = EVP_DigestUpdate(hash->md, pbData, cbDataLen) == 1;
    }
    LeaveCriticalSection(&g_lock);
    if (!found) {
        return fail(NTE_BAD_HASH);
    }
    return ok ? TRUE : fail(NTE_FAIL);
}

static BOOL copy_out(LPBYTE pbData, LPDWORD pcbDataLen, const void *src,
                     DWORD len)
{
    if (pcbDataLen == NULL) {
        return fail(ERROR_INVALID_PARAMETER);
    }
    if (pbData == NULL || *pcbDataLen < len) {
        *pcbDataLen = len;
        return fail(ERROR_MORE_DATA);
    }
    memcpy(pbData, src, len);
    *pcbDataLen = len;
    return TRUE;
}

ANEM_CSP_API BOOL WINAPI CPGetHashParam(HCRYPTPROV hProv, HCRYPTHASH hHash,
                                        DWORD dwParam, LPBYTE pbData,
                                        LPDWORD pcbDataLen, DWORD dwFlags)
{
    AnemCspHash *hash;
    BOOL result;

    (void)dwFlags;
    if (pcbDataLen == NULL) {
        return fail(ERROR_INVALID_PARAMETER);
    }
    EnterCriticalSection(&g_lock);
    hash = hash_lookup(hHash);
    if (hash == NULL || hash->prov != hProv || hash->md == NULL) {
        LeaveCriticalSection(&g_lock);
        return fail(NTE_BAD_HASH);
    }
    if (dwParam == HP_HASHSIZE) {
        DWORD size = (DWORD)EVP_MD_get_size(EVP_MD_CTX_get0_md(hash->md));

        result = size != 0u ? copy_out(pbData, pcbDataLen, &size,
                                       sizeof size)
                            : fail(NTE_FAIL);
    } else if (dwParam == HP_HASHVAL) {
        EVP_MD_CTX *tmp = EVP_MD_CTX_new();
        unsigned char digest[EVP_MAX_MD_SIZE];
        unsigned int digest_len = 0;
        int size = EVP_MD_get_size(EVP_MD_CTX_get0_md(hash->md));

        if (tmp == NULL || size <= 0) {
            result = fail(NTE_FAIL);
        } else if (EVP_MD_CTX_copy_ex(tmp, hash->md) != 1
                   || EVP_DigestFinal_ex(tmp, digest, &digest_len) != 1) {
            result = fail(NTE_FAIL);
        } else {
            result = copy_out(pbData, pcbDataLen, digest,
                              (DWORD)digest_len);
        }
        EVP_MD_CTX_free(tmp);
    } else {
        result = fail(NTE_INVALID_PARAMETER);
    }
    LeaveCriticalSection(&g_lock);
    return result;
}

ANEM_CSP_API BOOL WINAPI CPGetProvParam(HCRYPTPROV hProv, DWORD dwParam,
                                        LPBYTE pbData, LPDWORD pcbDataLen,
                                        DWORD dwFlags)
{
    static const char k_name[] = "Anemos OpenSSL CSP";
    static const struct {
        ALG_ID alg;
        const char *name;
    } algs[] = {
        { CALG_MD5, "MD5" },
        { CALG_SHA1, "SHA1" },
        { CALG_SHA_256, "SHA256" },
    };
    AnemCspContext *ctx;
    BOOL result;

    if (pcbDataLen == NULL) {
        return fail(ERROR_INVALID_PARAMETER);
    }
    EnterCriticalSection(&g_lock);
    ctx = ctx_lookup(hProv);
    if (ctx == NULL) {
        LeaveCriticalSection(&g_lock);
        return fail(NTE_BAD_KEYSET);
    }
    if (dwParam == PP_NAME) {
        result = copy_out(pbData, pcbDataLen, k_name, (DWORD)sizeof k_name);
    } else if (dwParam == PP_VERSION) {
        DWORD version = 0x00000200u;

        result = copy_out(pbData, pcbDataLen, &version, sizeof version);
    } else if (dwParam == PP_PROVTYPE) {
        DWORD type = PROV_RSA_FULL;

        result = copy_out(pbData, pcbDataLen, &type, sizeof type);
    } else if (dwParam == PP_IMPTYPE) {
        DWORD impl = CRYPT_IMPL_SOFTWARE;

        result = copy_out(pbData, pcbDataLen, &impl, sizeof impl);
    } else if (dwParam == PP_ENUMALGS) {
        if ((dwFlags & CRYPT_FIRST) != 0) {
            ctx->enum_index = 0;
        } else if ((dwFlags & CRYPT_NEXT) == 0) {
            LeaveCriticalSection(&g_lock);
            return fail(NTE_BAD_FLAGS);
        }
        if (ctx->enum_index >= sizeof algs / sizeof algs[0]) {
            LeaveCriticalSection(&g_lock);
            return fail(NTE_NO_MORE_ITEMS);
        }
        {
            PROV_ENUMALGS entry;
            size_t name_len = strlen(algs[ctx->enum_index].name) + 1u;

            memset(&entry, 0, sizeof entry);
            entry.aiAlgid = algs[ctx->enum_index].alg;
            entry.dwBitLen = 0;
            entry.dwNameLen = (DWORD)name_len;
            memcpy(entry.szName, algs[ctx->enum_index].name, name_len);
            result = copy_out(pbData, pcbDataLen, &entry, sizeof entry);
            if (result) {
                ctx->enum_index++;
            }
        }
    } else {
        result = fail(NTE_INVALID_PARAMETER);
    }
    LeaveCriticalSection(&g_lock);
    return result;
}

ANEM_CSP_API BOOL WINAPI CPSetProvParam(HCRYPTPROV hProv, DWORD dwParam,
                                        const BYTE *pbData, DWORD dwFlags)
{
    (void)hProv;
    (void)dwParam;
    (void)pbData;
    (void)dwFlags;
    return fail(NTE_INVALID_PARAMETER);
}

ANEM_CSP_API BOOL WINAPI CPDestroyHash(HCRYPTPROV hProv, HCRYPTHASH hHash)
{
    AnemCspHash *hash;
    BOOL found = FALSE;

    EnterCriticalSection(&g_lock);
    hash = hash_lookup(hHash);
    if (hash != NULL && hash->prov == hProv) {
        EVP_MD_CTX_free(hash->md);
        memset(hash, 0, sizeof *hash);
        found = TRUE;
    }
    LeaveCriticalSection(&g_lock);
    return found ? TRUE : fail(NTE_BAD_HASH);
}

ANEM_CSP_API BOOL WINAPI CPDuplicateHash(HCRYPTPROV hProv, HCRYPTHASH hHash,
                                         LPDWORD pdwReserved, DWORD dwFlags,
                                         HCRYPTHASH *phHash)
{
    AnemCspHash *src;
    AnemCspHash *slot = NULL;
    EVP_MD_CTX *copy;
    BOOL found = FALSE;

    (void)pdwReserved;
    (void)dwFlags;
    if (phHash == NULL) {
        return fail(ERROR_INVALID_PARAMETER);
    }
    *phHash = 0;
    copy = EVP_MD_CTX_new();
    if (copy == NULL) {
        return fail(NTE_NO_MEMORY);
    }
    EnterCriticalSection(&g_lock);
    src = hash_lookup(hHash);
    if (src != NULL && src->prov == hProv
        && EVP_MD_CTX_copy_ex(copy, src->md) == 1) {
        for (unsigned i = 0; i < ANEM_CSP_SLOTS; i++) {
            if (!g_hash[i].used) {
                slot = &g_hash[i];
                break;
            }
        }
    }
    if (slot != NULL) {
        memset(slot, 0, sizeof *slot);
        slot->used = true;
        slot->prov = hProv;
        slot->alg = src->alg;
        slot->md = copy;
        *phHash = hash_handle(slot);
        found = TRUE;
    }
    LeaveCriticalSection(&g_lock);
    if (!found) {
        EVP_MD_CTX_free(copy);
        return fail(NTE_BAD_HASH);
    }
    return TRUE;
}

ANEM_CSP_API BOOL WINAPI CPSetHashParam(HCRYPTPROV hProv, HCRYPTHASH hHash,
                                        DWORD dwParam, const BYTE *pbData,
                                        DWORD dwFlags)
{
    (void)hProv;
    (void)hHash;
    (void)dwParam;
    (void)pbData;
    (void)dwFlags;
    return fail(NTE_INVALID_PARAMETER);
}

/* ---- key operations: explicit stubs for the next slice ---- */

ANEM_CSP_API BOOL WINAPI CPGenKey(HCRYPTPROV hProv, ALG_ID Algid,
                                  DWORD dwFlags, HCRYPTKEY *phKey)
{
    (void)hProv;
    (void)Algid;
    (void)dwFlags;
    (void)phKey;
    return fail(NTE_NOT_SUPPORTED);
}

ANEM_CSP_API BOOL WINAPI CPDeriveKey(HCRYPTPROV hProv, ALG_ID Algid,
                                     HCRYPTHASH hHash, DWORD dwFlags,
                                     HCRYPTKEY *phKey)
{
    (void)hProv;
    (void)Algid;
    (void)hHash;
    (void)dwFlags;
    (void)phKey;
    return fail(NTE_NOT_SUPPORTED);
}

ANEM_CSP_API BOOL WINAPI CPDestroyKey(HCRYPTPROV hProv, HCRYPTKEY hKey)
{
    (void)hProv;
    (void)hKey;
    return fail(NTE_NOT_SUPPORTED);
}

ANEM_CSP_API BOOL WINAPI CPSetKeyParam(HCRYPTPROV hProv, HCRYPTKEY hKey,
                                       DWORD dwParam, const BYTE *pbData,
                                       DWORD dwFlags)
{
    (void)hProv;
    (void)hKey;
    (void)dwParam;
    (void)pbData;
    (void)dwFlags;
    return fail(NTE_NOT_SUPPORTED);
}

ANEM_CSP_API BOOL WINAPI CPGetKeyParam(HCRYPTPROV hProv, HCRYPTKEY hKey,
                                       DWORD dwParam, LPBYTE pbData,
                                       LPDWORD pcbDataLen, DWORD dwFlags)
{
    (void)hProv;
    (void)hKey;
    (void)dwParam;
    (void)pbData;
    (void)pcbDataLen;
    (void)dwFlags;
    return fail(NTE_NOT_SUPPORTED);
}

ANEM_CSP_API BOOL WINAPI CPExportKey(HCRYPTPROV hProv, HCRYPTKEY hKey,
                                     HCRYPTKEY hPubKey, DWORD dwBlobType,
                                     DWORD dwFlags, LPBYTE pbData,
                                     LPDWORD pcbDataLen)
{
    (void)hProv;
    (void)hKey;
    (void)hPubKey;
    (void)dwBlobType;
    (void)dwFlags;
    (void)pbData;
    (void)pcbDataLen;
    return fail(NTE_NOT_SUPPORTED);
}

ANEM_CSP_API BOOL WINAPI CPImportKey(HCRYPTPROV hProv, const BYTE *pbData,
                                     DWORD cbDataLen, HCRYPTKEY hPubKey,
                                     DWORD dwFlags, HCRYPTKEY *phKey)
{
    (void)hProv;
    (void)pbData;
    (void)cbDataLen;
    (void)hPubKey;
    (void)dwFlags;
    (void)phKey;
    return fail(NTE_NOT_SUPPORTED);
}

ANEM_CSP_API BOOL WINAPI CPEncrypt(HCRYPTPROV hProv, HCRYPTKEY hKey,
                                   HCRYPTHASH hHash, BOOL fFinal,
                                   DWORD dwFlags, LPBYTE pbData,
                                   LPDWORD pcbDataLen, DWORD cbBufLen)
{
    (void)hProv;
    (void)hKey;
    (void)hHash;
    (void)fFinal;
    (void)dwFlags;
    (void)pbData;
    (void)pcbDataLen;
    (void)cbBufLen;
    return fail(NTE_NOT_SUPPORTED);
}

ANEM_CSP_API BOOL WINAPI CPDecrypt(HCRYPTPROV hProv, HCRYPTKEY hKey,
                                   HCRYPTHASH hHash, BOOL fFinal,
                                   DWORD dwFlags, LPBYTE pbData,
                                   LPDWORD pcbDataLen)
{
    (void)hProv;
    (void)hKey;
    (void)hHash;
    (void)fFinal;
    (void)dwFlags;
    (void)pbData;
    (void)pcbDataLen;
    return fail(NTE_NOT_SUPPORTED);
}

ANEM_CSP_API BOOL WINAPI CPHashSessionKey(HCRYPTPROV hProv, HCRYPTHASH hHash,
                                          HCRYPTKEY hKey, DWORD dwFlags)
{
    (void)hProv;
    (void)hHash;
    (void)hKey;
    (void)dwFlags;
    return fail(NTE_NOT_SUPPORTED);
}

ANEM_CSP_API BOOL WINAPI CPSignHash(HCRYPTPROV hProv, HCRYPTHASH hHash,
                                    DWORD dwKeySpec, LPCWSTR szDescription,
                                    DWORD dwFlags, LPBYTE pbSignature,
                                    LPDWORD pcbSigLen)
{
    (void)hProv;
    (void)hHash;
    (void)dwKeySpec;
    (void)szDescription;
    (void)dwFlags;
    (void)pbSignature;
    (void)pcbSigLen;
    return fail(NTE_NOT_SUPPORTED);
}

ANEM_CSP_API BOOL WINAPI CPVerifySignature(HCRYPTPROV hProv,
                                           HCRYPTHASH hHash,
                                           const BYTE *pbSignature,
                                           DWORD cbSigLen, HCRYPTKEY hPubKey,
                                           LPCWSTR szDescription,
                                           DWORD dwFlags)
{
    (void)hProv;
    (void)hHash;
    (void)pbSignature;
    (void)cbSigLen;
    (void)hPubKey;
    (void)szDescription;
    (void)dwFlags;
    return fail(NTE_NOT_SUPPORTED);
}

ANEM_CSP_API BOOL WINAPI CPGetUserKey(HCRYPTPROV hProv, DWORD dwKeySpec,
                                      HCRYPTKEY *phUserKey)
{
    (void)hProv;
    (void)dwKeySpec;
    (void)phUserKey;
    return fail(NTE_NOT_SUPPORTED);
}

ANEM_CSP_API BOOL WINAPI CPDuplicateKey(HCRYPTPROV hProv, HCRYPTKEY hKey,
                                        LPDWORD pdwReserved, DWORD dwFlags,
                                        HCRYPTKEY *phKey)
{
    (void)hProv;
    (void)hKey;
    (void)pdwReserved;
    (void)dwFlags;
    (void)phKey;
    return fail(NTE_NOT_SUPPORTED);
}
