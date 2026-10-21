/* Anemos CSP — contexts, hashing, randomness, provider properties, and AES
 * key objects with one-shot encrypt/decrypt. Asymmetric operations remain
 * explicit stubs.
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

#include <openssl/bn.h>
#include <openssl/core_names.h>
#include <openssl/evp.h>
#include <openssl/param_build.h>
#include <openssl/rsa.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#define ANEM_CSP_SLOTS 32u
#define ANEM_CTX_BASE ((ULONG_PTR)0x00010000u)
#define ANEM_HASH_BASE ((ULONG_PTR)0x00020000u)
#define ANEM_KEY_BASE ((ULONG_PTR)0x00030000u)
#define ANEM_BLOCK_SIZE 16u
#define ANEM_MAX_KEY_LEN 32u
#define ANEM_RSA_BITS 1024
#define ANEM_RSA1_MAGIC 0x31415352u
#define ANEM_RSA2_MAGIC 0x32415352u

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

typedef struct AnemCspKey {
    bool used;
    HCRYPTPROV prov;
    ALG_ID alg;
    DWORD flags;
    DWORD mode;
    DWORD padding;
    DWORD key_len;
    BYTE iv[ANEM_BLOCK_SIZE];
    BYTE key[ANEM_MAX_KEY_LEN];
    EVP_CIPHER_CTX *stream;
    int stream_dir; /* 0 none, 1 encrypt, 2 decrypt */
    EVP_PKEY *rsa;
    DWORD key_spec;  /* AT_SIGNATURE / AT_KEYEXCHANGE for user keys */
    bool user_key;
} AnemCspKey;

static CRITICAL_SECTION g_lock;
static bool g_ready;
static AnemCspContext g_ctx[ANEM_CSP_SLOTS];
static AnemCspHash g_hash[ANEM_CSP_SLOTS];
static AnemCspKey g_key[ANEM_CSP_SLOTS];

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

static AnemCspKey *key_lookup(HCRYPTKEY hKey)
{
    ULONG_PTR index;

    if (hKey < ANEM_KEY_BASE || hKey >= ANEM_KEY_BASE + ANEM_CSP_SLOTS) {
        return NULL;
    }
    index = hKey - ANEM_KEY_BASE;
    return g_key[index].used ? &g_key[index] : NULL;
}

static HCRYPTKEY key_handle(const AnemCspKey *key)
{
    return (HCRYPTKEY)(ANEM_KEY_BASE + (ULONG_PTR)(key - g_key));
}

static void secure_zero(void *dst, size_t n)
{
    volatile BYTE *p = (volatile BYTE *)dst;

    for (size_t i = 0; i < n; i++) {
        p[i] = 0;
    }
}

static void key_reset_stream(AnemCspKey *key)
{
    EVP_CIPHER_CTX_free(key->stream);
    key->stream = NULL;
    key->stream_dir = 0;
}

static void key_clear(AnemCspKey *key)
{
    key_reset_stream(key);
    EVP_PKEY_free(key->rsa);
    key->rsa = NULL;
    secure_zero(key, sizeof *key);
}

static void destroy_keys_of(HCRYPTPROV hProv)
{
    for (unsigned i = 0; i < ANEM_CSP_SLOTS; i++) {
        if (g_key[i].used && g_key[i].prov == hProv) {
            key_clear(&g_key[i]);
        }
    }
}

static DWORD aes_key_len(ALG_ID alg)
{
    switch (alg) {
    case CALG_AES_128:
        return 16;
    case CALG_AES_192:
        return 24;
    case CALG_AES_256:
        return 32;
    default:
        return 0;
    }
}

static const EVP_CIPHER *aes_cipher(ALG_ID alg, DWORD mode)
{
    bool cbc = mode == CRYPT_MODE_CBC;

    switch (alg) {
    case CALG_AES_128:
        return cbc ? EVP_aes_128_cbc() : EVP_aes_128_ecb();
    case CALG_AES_192:
        return cbc ? EVP_aes_192_cbc() : EVP_aes_192_ecb();
    case CALG_AES_256:
        return cbc ? EVP_aes_256_cbc() : EVP_aes_256_ecb();
    default:
        return NULL;
    }
}

static void key_defaults(AnemCspKey *key, ALG_ID alg, DWORD flags)
{
    memset(key, 0, sizeof *key);
    key->used = true;
    key->alg = alg;
    key->flags = flags & CRYPT_EXPORTABLE;
    key->mode = CRYPT_MODE_CBC;
    key->padding = PKCS5_PADDING;
    key->key_len = aes_key_len(alg);
}

/* Start (or continue) a streaming operation. EVP buffers a partial block
 * internally, so CPEncrypt/CPDecrypt can be called with arbitrary chunk sizes
 * and still emit whole blocks, as CryptoAPI expects. */
static bool stream_start(AnemCspKey *key, int dir)
{
    const EVP_CIPHER *cipher;

    if (key->stream != NULL && key->stream_dir == dir) {
        return true;
    }
    key_reset_stream(key);
    cipher = aes_cipher(key->alg, key->mode);
    if (cipher == NULL) {
        return false;
    }
    key->stream = EVP_CIPHER_CTX_new();
    if (key->stream == NULL) {
        return false;
    }
    if (dir == 1) {
        if (EVP_EncryptInit_ex(key->stream, cipher, NULL, key->key,
                               key->mode == CRYPT_MODE_CBC ? key->iv : NULL)
            != 1) {
            key_reset_stream(key);
            return false;
        }
    } else {
        if (EVP_DecryptInit_ex(key->stream, cipher, NULL, key->key,
                               key->mode == CRYPT_MODE_CBC ? key->iv : NULL)
            != 1) {
            key_reset_stream(key);
            return false;
        }
    }
    if (key->padding == ZERO_PADDING) {
        (void)EVP_CIPHER_CTX_set_padding(key->stream, 0);
    }
    key->stream_dir = dir;
    return true;
}

static EVP_PKEY *rsa_generate(void)
{
    EVP_PKEY_CTX *ctx = EVP_PKEY_CTX_new_id(EVP_PKEY_RSA, NULL);
    EVP_PKEY *pkey = NULL;

    if (ctx == NULL) {
        return NULL;
    }
    if (EVP_PKEY_keygen_init(ctx) != 1
        || EVP_PKEY_CTX_set_rsa_keygen_bits(ctx, ANEM_RSA_BITS) != 1
        || EVP_PKEY_keygen(ctx, &pkey) != 1) {
        EVP_PKEY_free(pkey);
        pkey = NULL;
    }
    EVP_PKEY_CTX_free(ctx);
    return pkey;
}

static bool rsa_key_init(AnemCspKey *key, ALG_ID alg, DWORD flags,
                         EVP_PKEY *pkey)
{
    if (pkey == NULL) {
        return false;
    }
    memset(key, 0, sizeof *key);
    key->used = true;
    key->alg = alg;
    key->flags = flags & CRYPT_EXPORTABLE;
    key->rsa = pkey;
    key->key_len = (DWORD)(EVP_PKEY_get_bits(pkey) / 8);
    return true;
}

static BIGNUM *rsa_get_bn(EVP_PKEY *pkey, const char *name)
{
    BIGNUM *bn = NULL;

    return EVP_PKEY_get_bn_param(pkey, name, &bn) == 1 ? bn : NULL;
}

static bool put_bn_le(BYTE **p, const BIGNUM *bn, DWORD len)
{
    if (bn == NULL || BN_bn2lebinpad(bn, *p, (int)len) != (int)len) {
        return false;
    }
    *p += len;
    return true;
}

static bool bn_to_dword(const BIGNUM *bn, DWORD *out)
{
    BYTE buf[4];
    DWORD value = 0;
    int n;

    if (bn == NULL) {
        return false;
    }
    n = BN_num_bytes(bn);
    if (n <= 0 || n > 4 || BN_bn2bin(bn, buf) != n) {
        return false;
    }
    for (int i = 0; i < n; i++) {
        value = (value << 8) | (DWORD)buf[i];
    }
    *out = value;
    return true;
}

static BOOL rsa_export_blob(AnemCspKey *key, DWORD blob_type, LPBYTE pbData,
                            LPDWORD pcbDataLen)
{
    EVP_PKEY *pkey = key->rsa;
    BIGNUM *n = NULL;
    BIGNUM *e = NULL;
    BIGNUM *p = NULL;
    BIGNUM *q = NULL;
    BIGNUM *dmp1 = NULL;
    BIGNUM *dmq1 = NULL;
    BIGNUM *iqmp = NULL;
    BIGNUM *d = NULL;
    BLOBHEADER header;
    RSAPUBKEY rsa_header;
    BYTE *out;
    int bits;
    DWORD n_len;
    DWORD p_len;
    DWORD needed;
    DWORD pubexp = 0;
    BOOL ok = FALSE;

    if (pcbDataLen == NULL) {
        return fail(ERROR_INVALID_PARAMETER);
    }
    if (pkey == NULL) {
        return fail(NTE_BAD_KEY);
    }
    bits = EVP_PKEY_get_bits(pkey);
    if (bits < 512 || bits > 16384 || bits % 16 != 0) {
        return fail(NTE_FAIL);
    }
    n_len = (DWORD)bits / 8u;
    p_len = (DWORD)bits / 16u;
    if (blob_type == PUBLICKEYBLOB) {
        needed = (DWORD)sizeof header + (DWORD)sizeof rsa_header + n_len;
    } else if (blob_type == PRIVATEKEYBLOB) {
        if ((key->flags & CRYPT_EXPORTABLE) == 0u) {
            return fail(NTE_PERM);
        }
        needed = (DWORD)sizeof header + (DWORD)sizeof rsa_header + n_len
               + 5u * p_len + n_len;
    } else {
        return fail(NTE_BAD_TYPE);
    }
    if (pbData == NULL || *pcbDataLen < needed) {
        *pcbDataLen = needed;
        return fail(ERROR_MORE_DATA);
    }

    n = rsa_get_bn(pkey, OSSL_PKEY_PARAM_RSA_N);
    e = rsa_get_bn(pkey, OSSL_PKEY_PARAM_RSA_E);
    if (n == NULL || e == NULL || !bn_to_dword(e, &pubexp)) {
        goto done;
    }
    memset(&header, 0, sizeof header);
    header.bType = (BYTE)blob_type;
    header.bVersion = CUR_BLOB_VERSION;
    header.aiKeyAlg = key->alg;
    memset(&rsa_header, 0, sizeof rsa_header);
    rsa_header.magic = (blob_type == PUBLICKEYBLOB) ? ANEM_RSA1_MAGIC
                                                    : ANEM_RSA2_MAGIC;
    rsa_header.bitlen = (DWORD)bits;
    rsa_header.pubexp = pubexp;
    out = pbData;
    memcpy(out, &header, sizeof header);
    out += sizeof header;
    memcpy(out, &rsa_header, sizeof rsa_header);
    out += sizeof rsa_header;
    if (!put_bn_le(&out, n, n_len)) {
        goto done;
    }
    if (blob_type == PRIVATEKEYBLOB) {
        p = rsa_get_bn(pkey, OSSL_PKEY_PARAM_RSA_FACTOR1);
        q = rsa_get_bn(pkey, OSSL_PKEY_PARAM_RSA_FACTOR2);
        dmp1 = rsa_get_bn(pkey, OSSL_PKEY_PARAM_RSA_EXPONENT1);
        dmq1 = rsa_get_bn(pkey, OSSL_PKEY_PARAM_RSA_EXPONENT2);
        iqmp = rsa_get_bn(pkey, OSSL_PKEY_PARAM_RSA_COEFFICIENT1);
        d = rsa_get_bn(pkey, OSSL_PKEY_PARAM_RSA_D);
        if (!put_bn_le(&out, p, p_len) || !put_bn_le(&out, q, p_len)
            || !put_bn_le(&out, dmp1, p_len) || !put_bn_le(&out, dmq1, p_len)
            || !put_bn_le(&out, iqmp, p_len) || !put_bn_le(&out, d, n_len)) {
            goto done;
        }
    }
    *pcbDataLen = needed;
    ok = TRUE;
done:
    BN_free(n);
    BN_free(e);
    BN_free(p);
    BN_free(q);
    BN_free(dmp1);
    BN_free(dmq1);
    BN_free(iqmp);
    BN_free(d);
    return ok ? TRUE : fail(NTE_FAIL);
}

static EVP_PKEY *rsa_import_public(const BYTE *mod, DWORD mod_len, DWORD pubexp)
{
    BIGNUM *n = BN_lebin2bn(mod, (int)mod_len, NULL);
    BIGNUM *e = BN_new();
    OSSL_PARAM_BLD *bld = NULL;
    OSSL_PARAM *params = NULL;
    EVP_PKEY_CTX *ctx = NULL;
    EVP_PKEY *pkey = NULL;

    if (n == NULL || e == NULL || BN_set_word(e, pubexp) != 1) {
        goto done;
    }
    bld = OSSL_PARAM_BLD_new();
    if (bld == NULL
        || OSSL_PARAM_BLD_push_BN(bld, OSSL_PKEY_PARAM_RSA_N, n) != 1
        || OSSL_PARAM_BLD_push_BN(bld, OSSL_PKEY_PARAM_RSA_E, e) != 1) {
        goto done;
    }
    params = OSSL_PARAM_BLD_to_param(bld);
    if (params == NULL) {
        goto done;
    }
    ctx = EVP_PKEY_CTX_new_from_name(NULL, "RSA", NULL);
    if (ctx == NULL) {
        goto done;
    }
    if (EVP_PKEY_fromdata_init(ctx) != 1
        || EVP_PKEY_fromdata(ctx, &pkey, EVP_PKEY_PUBLIC_KEY, params) != 1) {
        EVP_PKEY_free(pkey);
        pkey = NULL;
    }
done:
    EVP_PKEY_CTX_free(ctx);
    OSSL_PARAM_free(params);
    OSSL_PARAM_BLD_free(bld);
    BN_free(n);
    BN_free(e);
    return pkey;
}

static EVP_PKEY *rsa_import_private(const BYTE *mod, const BYTE *p,
                                    const BYTE *q, const BYTE *dmp1,
                                    const BYTE *dmq1, const BYTE *iqmp,
                                    const BYTE *d, DWORD n_len, DWORD p_len,
                                    DWORD pubexp)
{
    BIGNUM *bn_n = BN_lebin2bn(mod, (int)n_len, NULL);
    BIGNUM *bn_p = BN_lebin2bn(p, (int)p_len, NULL);
    BIGNUM *bn_q = BN_lebin2bn(q, (int)p_len, NULL);
    BIGNUM *bn_dmp1 = BN_lebin2bn(dmp1, (int)p_len, NULL);
    BIGNUM *bn_dmq1 = BN_lebin2bn(dmq1, (int)p_len, NULL);
    BIGNUM *bn_iqmp = BN_lebin2bn(iqmp, (int)p_len, NULL);
    BIGNUM *bn_d = BN_lebin2bn(d, (int)n_len, NULL);
    BIGNUM *bn_e = BN_new();
    OSSL_PARAM_BLD *bld = NULL;
    OSSL_PARAM *params = NULL;
    EVP_PKEY_CTX *ctx = NULL;
    EVP_PKEY *pkey = NULL;

    if (bn_n == NULL || bn_p == NULL || bn_q == NULL || bn_dmp1 == NULL
        || bn_dmq1 == NULL || bn_iqmp == NULL || bn_d == NULL || bn_e == NULL
        || BN_set_word(bn_e, pubexp) != 1) {
        goto done;
    }
    bld = OSSL_PARAM_BLD_new();
    if (bld == NULL
        || OSSL_PARAM_BLD_push_BN(bld, OSSL_PKEY_PARAM_RSA_N, bn_n) != 1
        || OSSL_PARAM_BLD_push_BN(bld, OSSL_PKEY_PARAM_RSA_E, bn_e) != 1
        || OSSL_PARAM_BLD_push_BN(bld, OSSL_PKEY_PARAM_RSA_D, bn_d) != 1
        || OSSL_PARAM_BLD_push_BN(bld, OSSL_PKEY_PARAM_RSA_FACTOR1, bn_p) != 1
        || OSSL_PARAM_BLD_push_BN(bld, OSSL_PKEY_PARAM_RSA_FACTOR2, bn_q) != 1
        || OSSL_PARAM_BLD_push_BN(bld, OSSL_PKEY_PARAM_RSA_EXPONENT1, bn_dmp1)
               != 1
        || OSSL_PARAM_BLD_push_BN(bld, OSSL_PKEY_PARAM_RSA_EXPONENT2, bn_dmq1)
               != 1
        || OSSL_PARAM_BLD_push_BN(bld, OSSL_PKEY_PARAM_RSA_COEFFICIENT1, bn_iqmp)
               != 1) {
        goto done;
    }
    params = OSSL_PARAM_BLD_to_param(bld);
    if (params == NULL) {
        goto done;
    }
    ctx = EVP_PKEY_CTX_new_from_name(NULL, "RSA", NULL);
    if (ctx == NULL) {
        goto done;
    }
    if (EVP_PKEY_fromdata_init(ctx) != 1
        || EVP_PKEY_fromdata(ctx, &pkey, EVP_PKEY_KEYPAIR, params) != 1) {
        EVP_PKEY_free(pkey);
        pkey = NULL;
    }
done:
    EVP_PKEY_CTX_free(ctx);
    OSSL_PARAM_free(params);
    OSSL_PARAM_BLD_free(bld);
    BN_free(bn_n);
    BN_free(bn_p);
    BN_free(bn_q);
    BN_free(bn_dmp1);
    BN_free(bn_dmq1);
    BN_free(bn_iqmp);
    BN_free(bn_d);
    BN_free(bn_e);
    return pkey;
}

/* CryptoAPI signatures are little-endian: rsaenh reverses the big-endian
 * PKCS#1 integer before returning it, and expects the caller's signature in
 * that order. Both directions go through this helper, so a signature produced
 * here verifies in XP's own provider (and vice versa) instead of only against
 * itself. Proven against Wine's builtin rsaenh in anemos/csp/api_run.sh. */
#define ANEM_CSP_MAX_SIG 2048u   /* 16384-bit key, the largest import allows */

static void reverse_bytes(BYTE *buf, DWORD len)
{
    DWORD i = 0;
    DWORD j = len;

    while (i + 1 < j) {
        BYTE tmp = buf[i];

        j--;
        buf[i] = buf[j];
        buf[j] = tmp;
        i++;
    }
}

static BOOL rsa_sign_digest(EVP_PKEY *pkey, const EVP_MD *md,
                            const BYTE *digest, DWORD digest_len,
                            LPBYTE pbSignature, LPDWORD pcbSigLen)
{
    EVP_PKEY_CTX *ctx;
    size_t sig_len;
    BOOL ok;

    if (pkey == NULL || md == NULL || pcbSigLen == NULL) {
        return fail(ERROR_INVALID_PARAMETER);
    }
    sig_len = (size_t)EVP_PKEY_size(pkey);
    if (pbSignature == NULL || *pcbSigLen < (DWORD)sig_len) {
        *pcbSigLen = (DWORD)sig_len;
        return fail(ERROR_MORE_DATA);
    }
    ctx = EVP_PKEY_CTX_new(pkey, NULL);
    if (ctx == NULL) {
        return fail(NTE_NO_MEMORY);
    }
    ok = EVP_PKEY_sign_init(ctx) == 1
      && EVP_PKEY_CTX_set_rsa_padding(ctx, RSA_PKCS1_PADDING) == 1
      && EVP_PKEY_CTX_set_signature_md(ctx, md) == 1
      && EVP_PKEY_sign(ctx, pbSignature, &sig_len, digest, digest_len) == 1;
    EVP_PKEY_CTX_free(ctx);
    if (!ok) {
        return fail(NTE_FAIL);
    }
    *pcbSigLen = (DWORD)sig_len;
    return TRUE;
}

static BOOL rsa_verify_digest(EVP_PKEY *pkey, const EVP_MD *md,
                              const BYTE *digest, DWORD digest_len,
                              const BYTE *pbSignature, DWORD cbSigLen)
{
    EVP_PKEY_CTX *ctx;
    int result;

    if (pkey == NULL || md == NULL) {
        return fail(ERROR_INVALID_PARAMETER);
    }
    ctx = EVP_PKEY_CTX_new(pkey, NULL);
    if (ctx == NULL) {
        return fail(NTE_NO_MEMORY);
    }
    result = EVP_PKEY_verify_init(ctx) == 1
          && EVP_PKEY_CTX_set_rsa_padding(ctx, RSA_PKCS1_PADDING) == 1
          && EVP_PKEY_CTX_set_signature_md(ctx, md) == 1
          && EVP_PKEY_verify(ctx, pbSignature, cbSigLen, digest, digest_len);
    EVP_PKEY_CTX_free(ctx);
    if (result == 1) {
        return TRUE;
    }
    return fail(result == 0 ? NTE_BAD_SIGNATURE : NTE_FAIL);
}

/* The provider has no persistent keyset. A user key is generated lazily for
 * each key spec and lives for the provider context's lifetime. */
static AnemCspKey *user_key_for(HCRYPTPROV hProv, DWORD spec)
{
    AnemCspKey *slot = NULL;
    EVP_PKEY *pkey;
    ALG_ID alg;

    if (spec != AT_KEYEXCHANGE && spec != AT_SIGNATURE) {
        return NULL;
    }
    for (unsigned i = 0; i < ANEM_CSP_SLOTS; i++) {
        if (g_key[i].used && g_key[i].prov == hProv && g_key[i].user_key
            && g_key[i].key_spec == spec) {
            return &g_key[i];
        }
    }
    for (unsigned i = 0; i < ANEM_CSP_SLOTS; i++) {
        if (!g_key[i].used) {
            slot = &g_key[i];
            break;
        }
    }
    if (slot == NULL) {
        return NULL;
    }
    pkey = rsa_generate();
    if (pkey == NULL) {
        return NULL;
    }
    alg = (spec == AT_SIGNATURE) ? CALG_RSA_SIGN : CALG_RSA_KEYX;
    if (!rsa_key_init(slot, alg, CRYPT_EXPORTABLE, pkey)) {
        EVP_PKEY_free(pkey);
        return NULL;
    }
    slot->prov = hProv;
    slot->user_key = true;
    slot->key_spec = spec;
    return slot;
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
            if (g_key[i].used) {
                key_clear(&g_key[i]);
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
        destroy_keys_of(hProv);
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
    /* OpenSSL is built with no-threads so it loads on XP; every OpenSSL call
     * is serialised through this provider lock instead. */
    EnterCriticalSection(&g_lock);
    if (ctx_lookup(hProv) == NULL) {
        LeaveCriticalSection(&g_lock);
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
        return fail(NTE_NO_MEMORY);
    }
    md_ctx = EVP_MD_CTX_new();
    if (md_ctx == NULL) {
        LeaveCriticalSection(&g_lock);
        return fail(NTE_NO_MEMORY);
    }
    if (EVP_DigestInit_ex(md_ctx, md, NULL) != 1) {
        EVP_MD_CTX_free(md_ctx);
        LeaveCriticalSection(&g_lock);
        return fail(NTE_FAIL);
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
    EVP_MD_CTX *copy = NULL;
    BOOL found = FALSE;
    DWORD error = NTE_BAD_HASH;

    (void)pdwReserved;
    (void)dwFlags;
    if (phHash == NULL) {
        return fail(ERROR_INVALID_PARAMETER);
    }
    *phHash = 0;
    EnterCriticalSection(&g_lock);
    src = hash_lookup(hHash);
    if (src == NULL || src->prov != hProv) {
        error = NTE_BAD_HASH;
    } else {
        copy = EVP_MD_CTX_new();
        if (copy == NULL) {
            error = NTE_NO_MEMORY;
        } else if (EVP_MD_CTX_copy_ex(copy, src->md) != 1) {
            EVP_MD_CTX_free(copy);
            copy = NULL;
            error = NTE_FAIL;
        } else {
            for (unsigned i = 0; i < ANEM_CSP_SLOTS; i++) {
                if (!g_hash[i].used) {
                    slot = &g_hash[i];
                    break;
                }
            }
            if (slot == NULL) {
                error = NTE_NO_MEMORY;
            } else {
                memset(slot, 0, sizeof *slot);
                slot->used = true;
                slot->prov = hProv;
                slot->alg = src->alg;
                slot->md = copy;
                *phHash = hash_handle(slot);
                found = TRUE;
            }
        }
    }
    LeaveCriticalSection(&g_lock);
    if (!found) {
        EVP_MD_CTX_free(copy);
        return fail(error);
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

/* ---- symmetric key objects ---- */

ANEM_CSP_API BOOL WINAPI CPGenKey(HCRYPTPROV hProv, ALG_ID Algid,
                                  DWORD dwFlags, HCRYPTKEY *phKey)
{
    AnemCspKey *slot = NULL;
    DWORD key_len;

    if (phKey == NULL) {
        return fail(ERROR_INVALID_PARAMETER);
    }
    *phKey = 0;
    if (Algid == CALG_RSA_SIGN || Algid == CALG_RSA_KEYX) {
        EVP_PKEY *pkey;

        EnterCriticalSection(&g_lock);
        if (ctx_lookup(hProv) == NULL) {
            LeaveCriticalSection(&g_lock);
            return fail(NTE_BAD_KEYSET);
        }
        for (unsigned i = 0; i < ANEM_CSP_SLOTS; i++) {
            if (!g_key[i].used) {
                slot = &g_key[i];
                break;
            }
        }
        if (slot == NULL) {
            LeaveCriticalSection(&g_lock);
            return fail(NTE_NO_MEMORY);
        }
        pkey = rsa_generate();
        if (pkey == NULL) {
            LeaveCriticalSection(&g_lock);
            return fail(NTE_NO_MEMORY);
        }
        (void)rsa_key_init(slot, Algid, dwFlags, pkey);
        slot->prov = hProv;
        *phKey = key_handle(slot);
        LeaveCriticalSection(&g_lock);
        return TRUE;
    }
    key_len = aes_key_len(Algid);
    if (key_len == 0) {
        return fail(NTE_BAD_ALGID);
    }
    EnterCriticalSection(&g_lock);
    if (ctx_lookup(hProv) == NULL) {
        LeaveCriticalSection(&g_lock);
        return fail(NTE_BAD_KEYSET);
    }
    for (unsigned i = 0; i < ANEM_CSP_SLOTS; i++) {
        if (!g_key[i].used) {
            slot = &g_key[i];
            break;
        }
    }
    if (slot == NULL) {
        LeaveCriticalSection(&g_lock);
        return fail(NTE_NO_MEMORY);
    }
    key_defaults(slot, Algid, dwFlags);
    slot->prov = hProv;
    if (!SystemFunction036(slot->key, key_len)) {
        key_clear(slot);
        LeaveCriticalSection(&g_lock);
        return fail(ERROR_GEN_FAILURE);
    }
    *phKey = key_handle(slot);
    LeaveCriticalSection(&g_lock);
    return TRUE;
}

ANEM_CSP_API BOOL WINAPI CPDeriveKey(HCRYPTPROV hProv, ALG_ID Algid,
                                     HCRYPTHASH hHash, DWORD dwFlags,
                                     HCRYPTKEY *phKey)
{
    AnemCspKey *slot = NULL;
    AnemCspHash *hash;
    EVP_MD_CTX *tmp;
    unsigned char digest[EVP_MAX_MD_SIZE];
    unsigned int digest_len = 0;
    DWORD key_len = aes_key_len(Algid);
    DWORD error = NTE_FAIL;
    BOOL ok = FALSE;

    if (phKey == NULL) {
        return fail(ERROR_INVALID_PARAMETER);
    }
    *phKey = 0;
    if (key_len == 0) {
        return fail(NTE_BAD_ALGID);
    }
    EnterCriticalSection(&g_lock);
    if (ctx_lookup(hProv) == NULL) {
        LeaveCriticalSection(&g_lock);
        return fail(NTE_BAD_KEYSET);
    }
    hash = hash_lookup(hHash);
    if (hash == NULL || hash->prov != hProv || hash->md == NULL) {
        LeaveCriticalSection(&g_lock);
        return fail(NTE_BAD_HASH);
    }
    for (unsigned i = 0; i < ANEM_CSP_SLOTS; i++) {
        if (!g_key[i].used) {
            slot = &g_key[i];
            break;
        }
    }
    if (slot == NULL) {
        LeaveCriticalSection(&g_lock);
        return fail(NTE_NO_MEMORY);
    }
    tmp = EVP_MD_CTX_new();
    if (tmp == NULL) {
        LeaveCriticalSection(&g_lock);
        return fail(NTE_NO_MEMORY);
    }
    if (EVP_MD_CTX_copy_ex(tmp, hash->md) != 1
        || EVP_DigestFinal_ex(tmp, digest, &digest_len) != 1) {
        error = NTE_FAIL;
    } else if (digest_len < key_len) {
        error = NTE_BAD_LEN;
    } else {
        /* Documented derivation: the first key_len bytes of the hash value. */
        key_defaults(slot, Algid, dwFlags);
        slot->prov = hProv;
        memcpy(slot->key, digest, key_len);
        *phKey = key_handle(slot);
        ok = TRUE;
    }
    EVP_MD_CTX_free(tmp);
    secure_zero(digest, sizeof digest);
    LeaveCriticalSection(&g_lock);
    return ok ? TRUE : fail(error);
}

ANEM_CSP_API BOOL WINAPI CPDestroyKey(HCRYPTPROV hProv, HCRYPTKEY hKey)
{
    AnemCspKey *key;
    BOOL found = FALSE;

    EnterCriticalSection(&g_lock);
    key = key_lookup(hKey);
    if (key != NULL && key->prov == hProv) {
        key_clear(key);
        found = TRUE;
    }
    LeaveCriticalSection(&g_lock);
    return found ? TRUE : fail(NTE_BAD_KEY);
}

ANEM_CSP_API BOOL WINAPI CPSetKeyParam(HCRYPTPROV hProv, HCRYPTKEY hKey,
                                       DWORD dwParam, const BYTE *pbData,
                                       DWORD dwFlags)
{
    AnemCspKey *key;
    DWORD value = 0;
    BOOL ok = FALSE;

    (void)dwFlags;
    if (pbData == NULL) {
        return fail(ERROR_INVALID_PARAMETER);
    }
    EnterCriticalSection(&g_lock);
    key = key_lookup(hKey);
    if (key == NULL || key->prov != hProv) {
        LeaveCriticalSection(&g_lock);
        return fail(NTE_BAD_KEY);
    }
    if (key->rsa != NULL) {
        LeaveCriticalSection(&g_lock);
        return fail(NTE_INVALID_PARAMETER);
    }
    if (dwParam == KP_MODE) {
        memcpy(&value, pbData, sizeof value);
        if (value == CRYPT_MODE_CBC || value == CRYPT_MODE_ECB) {
            key->mode = value;
            key_reset_stream(key);
            ok = TRUE;
        }
    } else if (dwParam == KP_PADDING) {
        memcpy(&value, pbData, sizeof value);
        if (value == PKCS5_PADDING || value == ZERO_PADDING) {
            key->padding = value;
            key_reset_stream(key);
            ok = TRUE;
        }
    } else if (dwParam == KP_IV) {
        memcpy(key->iv, pbData, ANEM_BLOCK_SIZE);
        key_reset_stream(key);
        ok = TRUE;
    }
    LeaveCriticalSection(&g_lock);
    return ok ? TRUE : fail(NTE_INVALID_PARAMETER);
}

ANEM_CSP_API BOOL WINAPI CPGetKeyParam(HCRYPTPROV hProv, HCRYPTKEY hKey,
                                       DWORD dwParam, LPBYTE pbData,
                                       LPDWORD pcbDataLen, DWORD dwFlags)
{
    AnemCspKey *key;
    DWORD value = 0;
    BOOL result;

    (void)dwFlags;
    if (pcbDataLen == NULL) {
        return fail(ERROR_INVALID_PARAMETER);
    }
    EnterCriticalSection(&g_lock);
    key = key_lookup(hKey);
    if (key == NULL || key->prov != hProv) {
        LeaveCriticalSection(&g_lock);
        return fail(NTE_BAD_KEY);
    }
    if (key->rsa != NULL) {
        if (dwParam == KP_ALGID) {
            value = key->alg;
        } else if (dwParam == KP_KEYLEN) {
            value = key->key_len * 8u;
        } else {
            LeaveCriticalSection(&g_lock);
            return fail(NTE_INVALID_PARAMETER);
        }
        result = copy_out(pbData, pcbDataLen, &value, sizeof value);
        LeaveCriticalSection(&g_lock);
        return result;
    }
    if (dwParam == KP_IV) {
        result = copy_out(pbData, pcbDataLen, key->iv, ANEM_BLOCK_SIZE);
    } else {
        if (dwParam == KP_ALGID) {
            value = key->alg;
        } else if (dwParam == KP_KEYLEN) {
            value = key->key_len * 8u;
        } else if (dwParam == KP_BLOCKLEN) {
            value = ANEM_BLOCK_SIZE * 8u;
        } else if (dwParam == KP_MODE) {
            value = key->mode;
        } else if (dwParam == KP_PADDING) {
            value = key->padding;
        } else {
            LeaveCriticalSection(&g_lock);
            return fail(NTE_INVALID_PARAMETER);
        }
        result = copy_out(pbData, pcbDataLen, &value, sizeof value);
    }
    LeaveCriticalSection(&g_lock);
    return result;
}

ANEM_CSP_API BOOL WINAPI CPExportKey(HCRYPTPROV hProv, HCRYPTKEY hKey,
                                     HCRYPTKEY hPubKey, DWORD dwBlobType,
                                     DWORD dwFlags, LPBYTE pbData,
                                     LPDWORD pcbDataLen)
{
    AnemCspKey *key;
    BLOBHEADER header;
    DWORD needed;
    DWORD cb_key;
    BOOL result;

    (void)hPubKey;
    (void)dwFlags;
    if (pcbDataLen == NULL) {
        return fail(ERROR_INVALID_PARAMETER);
    }
    EnterCriticalSection(&g_lock);
    key = key_lookup(hKey);
    if (key == NULL || key->prov != hProv) {
        LeaveCriticalSection(&g_lock);
        return fail(NTE_BAD_KEY);
    }
    if (key->rsa != NULL) {
        result = rsa_export_blob(key, dwBlobType, pbData, pcbDataLen);
        LeaveCriticalSection(&g_lock);
        return result;
    }
    if (dwBlobType != PLAINTEXTKEYBLOB) {
        LeaveCriticalSection(&g_lock);
        return fail(NTE_BAD_TYPE);
    }
    if ((key->flags & CRYPT_EXPORTABLE) == 0u) {
        LeaveCriticalSection(&g_lock);
        return fail(NTE_PERM);
    }
    needed = (DWORD)sizeof header + (DWORD)sizeof cb_key + key->key_len;
    if (pbData == NULL || *pcbDataLen < needed) {
        *pcbDataLen = needed;
        LeaveCriticalSection(&g_lock);
        return fail(ERROR_MORE_DATA);
    }
    memset(&header, 0, sizeof header);
    header.bType = PLAINTEXTKEYBLOB;
    header.bVersion = CUR_BLOB_VERSION;
    header.aiKeyAlg = key->alg;
    cb_key = key->key_len;
    memcpy(pbData, &header, sizeof header);
    memcpy(pbData + sizeof header, &cb_key, sizeof cb_key);
    memcpy(pbData + sizeof header + sizeof cb_key, key->key, key->key_len);
    *pcbDataLen = needed;
    LeaveCriticalSection(&g_lock);
    return TRUE;
}

ANEM_CSP_API BOOL WINAPI CPImportKey(HCRYPTPROV hProv, const BYTE *pbData,
                                     DWORD cbDataLen, HCRYPTKEY hPubKey,
                                     DWORD dwFlags, HCRYPTKEY *phKey)
{
    AnemCspKey *slot = NULL;
    BLOBHEADER header;
    DWORD cb_key;
    DWORD key_len;

    (void)hPubKey;
    if (phKey == NULL) {
        return fail(ERROR_INVALID_PARAMETER);
    }
    *phKey = 0;
    if (pbData == NULL
        || cbDataLen < (DWORD)sizeof header + (DWORD)sizeof cb_key) {
        return fail(NTE_BAD_LEN);
    }
    memcpy(&header, pbData, sizeof header);
    if (header.bType == PUBLICKEYBLOB || header.bType == PRIVATEKEYBLOB) {
        RSAPUBKEY rsa_header;
        const BYTE *body;
        EVP_PKEY *pkey;
        DWORD n_len;
        DWORD p_len;

        if (header.bVersion != CUR_BLOB_VERSION) {
            return fail(NTE_BAD_VER);
        }
        if (cbDataLen < (DWORD)sizeof header + (DWORD)sizeof rsa_header) {
            return fail(NTE_BAD_LEN);
        }
        memcpy(&rsa_header, pbData + sizeof header, sizeof rsa_header);
        if (rsa_header.bitlen < 512 || rsa_header.bitlen > 16384
            || rsa_header.bitlen % 16u != 0u) {
            return fail(NTE_BAD_LEN);
        }
        n_len = rsa_header.bitlen / 8u;
        p_len = rsa_header.bitlen / 16u;
        body = pbData + sizeof header + sizeof rsa_header;
        if (header.bType == PUBLICKEYBLOB) {
            if (rsa_header.magic != ANEM_RSA1_MAGIC) {
                return fail(NTE_BAD_DATA);
            }
            if (cbDataLen != (DWORD)sizeof header + (DWORD)sizeof rsa_header
                             + n_len) {
                return fail(NTE_BAD_LEN);
            }
            pkey = rsa_import_public(body, n_len, rsa_header.pubexp);
        } else {
            if (rsa_header.magic != ANEM_RSA2_MAGIC) {
                return fail(NTE_BAD_DATA);
            }
            if (cbDataLen != (DWORD)sizeof header + (DWORD)sizeof rsa_header
                             + n_len + 5u * p_len + n_len) {
                return fail(NTE_BAD_LEN);
            }
            pkey = rsa_import_private(
                body, body + n_len, body + n_len + p_len,
                body + n_len + 2u * p_len, body + n_len + 3u * p_len,
                body + n_len + 4u * p_len, body + n_len + 5u * p_len,
                n_len, p_len, rsa_header.pubexp);
        }
        if (pkey == NULL) {
            return fail(NTE_FAIL);
        }
        if (header.aiKeyAlg != CALG_RSA_SIGN
            && header.aiKeyAlg != CALG_RSA_KEYX) {
            EVP_PKEY_free(pkey);
            return fail(NTE_BAD_ALGID);
        }
        EnterCriticalSection(&g_lock);
        if (ctx_lookup(hProv) == NULL) {
            LeaveCriticalSection(&g_lock);
            EVP_PKEY_free(pkey);
            return fail(NTE_BAD_KEYSET);
        }
        for (unsigned i = 0; i < ANEM_CSP_SLOTS; i++) {
            if (!g_key[i].used) {
                slot = &g_key[i];
                break;
            }
        }
        if (slot == NULL) {
            LeaveCriticalSection(&g_lock);
            EVP_PKEY_free(pkey);
            return fail(NTE_NO_MEMORY);
        }
        (void)rsa_key_init(slot, header.aiKeyAlg, dwFlags, pkey);
        slot->prov = hProv;
        *phKey = key_handle(slot);
        LeaveCriticalSection(&g_lock);
        return TRUE;
    }
    if (header.bType != PLAINTEXTKEYBLOB) {
        return fail(NTE_BAD_TYPE);
    }
    if (header.bVersion != CUR_BLOB_VERSION) {
        return fail(NTE_BAD_VER);
    }
    key_len = aes_key_len(header.aiKeyAlg);
    if (key_len == 0) {
        return fail(NTE_BAD_ALGID);
    }
    memcpy(&cb_key, pbData + sizeof header, sizeof cb_key);
    if (cb_key != key_len) {
        return fail(NTE_BAD_LEN);
    }
    if (cbDataLen != (DWORD)sizeof header + (DWORD)sizeof cb_key + cb_key) {
        return fail(NTE_BAD_LEN);
    }
    EnterCriticalSection(&g_lock);
    if (ctx_lookup(hProv) == NULL) {
        LeaveCriticalSection(&g_lock);
        return fail(NTE_BAD_KEYSET);
    }
    for (unsigned i = 0; i < ANEM_CSP_SLOTS; i++) {
        if (!g_key[i].used) {
            slot = &g_key[i];
            break;
        }
    }
    if (slot == NULL) {
        LeaveCriticalSection(&g_lock);
        return fail(NTE_NO_MEMORY);
    }
    key_defaults(slot, header.aiKeyAlg, dwFlags);
    slot->prov = hProv;
    memcpy(slot->key, pbData + sizeof header + sizeof cb_key, key_len);
    *phKey = key_handle(slot);
    LeaveCriticalSection(&g_lock);
    return TRUE;
}

ANEM_CSP_API BOOL WINAPI CPEncrypt(HCRYPTPROV hProv, HCRYPTKEY hKey,
                                   HCRYPTHASH hHash, BOOL fFinal,
                                   DWORD dwFlags, LPBYTE pbData,
                                   LPDWORD pcbDataLen, DWORD cbBufLen)
{
    AnemCspKey *key;
    BYTE *input;
    DWORD input_len;
    DWORD required;
    int out1 = 0;
    int out2 = 0;
    BOOL ok = FALSE;

    (void)dwFlags;
    if (pbData == NULL || pcbDataLen == NULL) {
        return fail(ERROR_INVALID_PARAMETER);
    }
    if (hHash != 0) {
        return fail(NTE_NOT_SUPPORTED);
    }
    input_len = *pcbDataLen;
    if (input_len > 0x7fffffffu) {
        return fail(NTE_BAD_LEN);
    }
    EnterCriticalSection(&g_lock);
    key = key_lookup(hKey);
    if (key == NULL || key->prov != hProv) {
        LeaveCriticalSection(&g_lock);
        return fail(NTE_BAD_KEY);
    }
    if (key->rsa != NULL) {
        LeaveCriticalSection(&g_lock);
        return fail(NTE_NOT_SUPPORTED);
    }
    if (key->padding != ZERO_PADDING && key->padding != PKCS5_PADDING) {
        LeaveCriticalSection(&g_lock);
        return fail(NTE_INVALID_PARAMETER);
    }
    /* EVP buffers a partial block, so a non-final call emits at most the
     * input length. Only the final PKCS#5 call can add a full block. */
    required = input_len;
    if (fFinal && key->padding == PKCS5_PADDING) {
        required += ANEM_BLOCK_SIZE;
    }
    if (required > cbBufLen || input_len > cbBufLen) {
        *pcbDataLen = required;
        LeaveCriticalSection(&g_lock);
        return fail(ERROR_MORE_DATA);
    }
    if (!stream_start(key, 1)) {
        LeaveCriticalSection(&g_lock);
        return fail(NTE_ENCRYPTION_FAILURE);
    }
    input = (BYTE *)malloc(input_len != 0u ? input_len : 1u);
    if (input == NULL) {
        LeaveCriticalSection(&g_lock);
        return fail(NTE_NO_MEMORY);
    }
    if (input_len != 0u) {
        memcpy(input, pbData, input_len);
    }
    if (EVP_EncryptUpdate(key->stream, pbData, &out1, input, (int)input_len) == 1
        && (!fFinal
            || EVP_EncryptFinal_ex(key->stream, pbData + out1, &out2) == 1)) {
        *pcbDataLen = (DWORD)(out1 + out2);
        ok = TRUE;
    }
    if (fFinal) {
        key_reset_stream(key);
    }
    secure_zero(input, input_len);
    free(input);
    LeaveCriticalSection(&g_lock);
    return ok ? TRUE : fail(NTE_ENCRYPTION_FAILURE);
}

ANEM_CSP_API BOOL WINAPI CPDecrypt(HCRYPTPROV hProv, HCRYPTKEY hKey,
                                   HCRYPTHASH hHash, BOOL fFinal,
                                   DWORD dwFlags, LPBYTE pbData,
                                   LPDWORD pcbDataLen)
{
    AnemCspKey *key;
    BYTE *input;
    DWORD input_len;
    int out1 = 0;
    int out2 = 0;
    BOOL ok = FALSE;

    (void)dwFlags;
    if (pbData == NULL || pcbDataLen == NULL) {
        return fail(ERROR_INVALID_PARAMETER);
    }
    if (hHash != 0) {
        return fail(NTE_NOT_SUPPORTED);
    }
    input_len = *pcbDataLen;
    if (input_len > 0x7fffffffu) {
        return fail(NTE_BAD_LEN);
    }
    EnterCriticalSection(&g_lock);
    key = key_lookup(hKey);
    if (key == NULL || key->prov != hProv) {
        LeaveCriticalSection(&g_lock);
        return fail(NTE_BAD_KEY);
    }
    if (key->rsa != NULL) {
        LeaveCriticalSection(&g_lock);
        return fail(NTE_NOT_SUPPORTED);
    }
    if (key->padding != ZERO_PADDING && key->padding != PKCS5_PADDING) {
        LeaveCriticalSection(&g_lock);
        return fail(NTE_INVALID_PARAMETER);
    }
    if (!stream_start(key, 2)) {
        LeaveCriticalSection(&g_lock);
        return fail(NTE_DECRYPTION_FAILURE);
    }
    input = (BYTE *)malloc(input_len != 0u ? input_len : 1u);
    if (input == NULL) {
        LeaveCriticalSection(&g_lock);
        return fail(NTE_NO_MEMORY);
    }
    if (input_len != 0u) {
        memcpy(input, pbData, input_len);
    }
    if (EVP_DecryptUpdate(key->stream, pbData, &out1, input, (int)input_len) == 1
        && (!fFinal
            || EVP_DecryptFinal_ex(key->stream, pbData + out1, &out2) == 1)) {
        *pcbDataLen = (DWORD)(out1 + out2);
        ok = TRUE;
    }
    if (fFinal) {
        key_reset_stream(key);
    }
    secure_zero(input, input_len);
    free(input);
    LeaveCriticalSection(&g_lock);
    return ok ? TRUE : fail(NTE_DECRYPTION_FAILURE);
}

ANEM_CSP_API BOOL WINAPI CPHashSessionKey(HCRYPTPROV hProv, HCRYPTHASH hHash,
                                          HCRYPTKEY hKey, DWORD dwFlags)
{
    AnemCspHash *hash;
    AnemCspKey *key;
    DWORD error = NTE_BAD_HASH;
    BOOL ok = FALSE;

    (void)dwFlags;
    EnterCriticalSection(&g_lock);
    hash = hash_lookup(hHash);
    key = key_lookup(hKey);
    if (hash != NULL && key != NULL && hash->prov == hProv
        && key->prov == hProv && key->rsa == NULL && hash->md != NULL) {
        error = NTE_FAIL;
        ok = EVP_DigestUpdate(hash->md, key->key, key->key_len) == 1;
    }
    LeaveCriticalSection(&g_lock);
    return ok ? TRUE : fail(error);
}

ANEM_CSP_API BOOL WINAPI CPSignHash(HCRYPTPROV hProv, HCRYPTHASH hHash,
                                    DWORD dwKeySpec, LPCWSTR szDescription,
                                    DWORD dwFlags, LPBYTE pbSignature,
                                    LPDWORD pcbSigLen)
{
    AnemCspHash *hash;
    AnemCspKey *key;
    EVP_MD_CTX *tmp;
    unsigned char digest[EVP_MAX_MD_SIZE];
    unsigned int digest_len = 0;
    const EVP_MD *md;
    BOOL result = FALSE;

    (void)szDescription;
    (void)dwFlags;
    if (pcbSigLen == NULL) {
        return fail(ERROR_INVALID_PARAMETER);
    }
    if (dwKeySpec != AT_SIGNATURE && dwKeySpec != AT_KEYEXCHANGE) {
        return fail(NTE_BAD_KEY);
    }
    EnterCriticalSection(&g_lock);
    hash = hash_lookup(hHash);
    if (hash == NULL || hash->prov != hProv || hash->md == NULL) {
        LeaveCriticalSection(&g_lock);
        return fail(NTE_BAD_HASH);
    }
    key = user_key_for(hProv, dwKeySpec);
    if (key == NULL || key->rsa == NULL) {
        LeaveCriticalSection(&g_lock);
        return fail(NTE_NO_MEMORY);
    }
    tmp = EVP_MD_CTX_new();
    if (tmp == NULL) {
        LeaveCriticalSection(&g_lock);
        return fail(NTE_NO_MEMORY);
    }
    if (EVP_MD_CTX_copy_ex(tmp, hash->md) == 1
        && EVP_DigestFinal_ex(tmp, digest, &digest_len) == 1) {
        md = EVP_MD_CTX_get0_md(hash->md);
        result = rsa_sign_digest(key->rsa, md, digest, (DWORD)digest_len,
                                 pbSignature, pcbSigLen);
        if (result != FALSE) {
            reverse_bytes(pbSignature, *pcbSigLen);
        }
    } else {
        result = fail(NTE_FAIL);
    }
    EVP_MD_CTX_free(tmp);
    secure_zero(digest, sizeof digest);
    LeaveCriticalSection(&g_lock);
    return result;
}

ANEM_CSP_API BOOL WINAPI CPVerifySignature(HCRYPTPROV hProv,
                                           HCRYPTHASH hHash,
                                           const BYTE *pbSignature,
                                           DWORD cbSigLen, HCRYPTKEY hPubKey,
                                           LPCWSTR szDescription,
                                           DWORD dwFlags)
{
    AnemCspHash *hash;
    AnemCspKey *key;
    EVP_MD_CTX *tmp;
    unsigned char digest[EVP_MAX_MD_SIZE];
    unsigned int digest_len = 0;
    const EVP_MD *md;
    BOOL result = FALSE;
    BYTE little[ANEM_CSP_MAX_SIG];

    (void)szDescription;
    (void)dwFlags;
    if (pbSignature == NULL || cbSigLen == 0u) {
        return fail(ERROR_INVALID_PARAMETER);
    }
    if (cbSigLen > sizeof little) {
        return fail(NTE_BAD_SIGNATURE);
    }
    memcpy(little, pbSignature, cbSigLen);
    reverse_bytes(little, cbSigLen);
    EnterCriticalSection(&g_lock);
    hash = hash_lookup(hHash);
    key = key_lookup(hPubKey);
    if (hash == NULL || hash->prov != hProv || hash->md == NULL) {
        LeaveCriticalSection(&g_lock);
        return fail(NTE_BAD_HASH);
    }
    if (key == NULL || key->prov != hProv || key->rsa == NULL) {
        LeaveCriticalSection(&g_lock);
        return fail(NTE_BAD_KEY);
    }
    tmp = EVP_MD_CTX_new();
    if (tmp == NULL) {
        LeaveCriticalSection(&g_lock);
        return fail(NTE_NO_MEMORY);
    }
    if (EVP_MD_CTX_copy_ex(tmp, hash->md) == 1
        && EVP_DigestFinal_ex(tmp, digest, &digest_len) == 1) {
        md = EVP_MD_CTX_get0_md(hash->md);
        result = rsa_verify_digest(key->rsa, md, digest, (DWORD)digest_len,
                                   little, cbSigLen);
    } else {
        result = fail(NTE_FAIL);
    }
    EVP_MD_CTX_free(tmp);
    secure_zero(digest, sizeof digest);
    LeaveCriticalSection(&g_lock);
    return result;
}

ANEM_CSP_API BOOL WINAPI CPGetUserKey(HCRYPTPROV hProv, DWORD dwKeySpec,
                                      HCRYPTKEY *phUserKey)
{
    AnemCspKey *key;

    if (phUserKey == NULL) {
        return fail(ERROR_INVALID_PARAMETER);
    }
    *phUserKey = 0;
    if (dwKeySpec != AT_SIGNATURE && dwKeySpec != AT_KEYEXCHANGE) {
        return fail(NTE_BAD_KEY);
    }
    EnterCriticalSection(&g_lock);
    if (ctx_lookup(hProv) == NULL) {
        LeaveCriticalSection(&g_lock);
        return fail(NTE_BAD_KEYSET);
    }
    key = user_key_for(hProv, dwKeySpec);
    if (key == NULL) {
        LeaveCriticalSection(&g_lock);
        return fail(NTE_NO_MEMORY);
    }
    *phUserKey = key_handle(key);
    LeaveCriticalSection(&g_lock);
    return TRUE;
}

ANEM_CSP_API BOOL WINAPI CPDuplicateKey(HCRYPTPROV hProv, HCRYPTKEY hKey,
                                        LPDWORD pdwReserved, DWORD dwFlags,
                                        HCRYPTKEY *phKey)
{
    AnemCspKey *src;
    AnemCspKey *slot = NULL;
    DWORD error = NTE_BAD_KEY;
    BOOL found = FALSE;

    (void)pdwReserved;
    (void)dwFlags;
    if (phKey == NULL) {
        return fail(ERROR_INVALID_PARAMETER);
    }
    *phKey = 0;
    EnterCriticalSection(&g_lock);
    src = key_lookup(hKey);
    if (src != NULL && src->prov == hProv) {
        error = NTE_NO_MEMORY;
        for (unsigned i = 0; i < ANEM_CSP_SLOTS; i++) {
            if (!g_key[i].used) {
                slot = &g_key[i];
                break;
            }
        }
        if (slot != NULL) {
            if (src->rsa != NULL && EVP_PKEY_up_ref(src->rsa) != 1) {
                error = NTE_FAIL;
            } else {
                *slot = *src;
                /* A duplicated key starts its own stream; sharing the EVP
                 * state would interleave two callers' blocks. */
                slot->stream = NULL;
                slot->stream_dir = 0;
                *phKey = key_handle(slot);
                found = TRUE;
            }
        }
    }
    LeaveCriticalSection(&g_lock);
    return found ? TRUE : fail(error);
}
