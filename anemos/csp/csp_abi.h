/* The CryptoSPI surface, declared for this project.
 *
 * Written from the provider API specification (the 23 required + 2 optional
 * entry points that advapi32 resolves by name). This header is ours; it is not
 * a copy of Microsoft's cspdk.h. Types come from the platform SDK
 * (wincrypt.h via windows.h), which is an interface, not an implementation.
 *
 * All entry points are WINAPI (__stdcall). Names are exported undecorated.
 */
#ifndef ANEMOS_CSP_ABI_H
#define ANEMOS_CSP_ABI_H

#include <windows.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Only Version, the two callbacks and the provider identity are needed by
 * this provider; the layout matches the documented vtable so a caller passing
 * a real one is safe. */
typedef struct AnemVTableProvStruc {
    DWORD Version;
    void *FuncVerifyImage;
    void *FuncReturnhWnd;
    DWORD dwProvType;
    BYTE *pbContextInfo;
    DWORD cbContextInfo;
    LPSTR pszProvName;
} AnemVTableProvStruc;

#ifdef ANEM_CSP_BUILD
#define ANEM_CSP_API __declspec(dllexport)
#else
#define ANEM_CSP_API
#endif

ANEM_CSP_API BOOL WINAPI CPAcquireContext(HCRYPTPROV *phProv,
                                          LPCSTR szContainer, DWORD dwFlags,
                                          AnemVTableProvStruc *pVTable);
ANEM_CSP_API BOOL WINAPI CPAcquireContextW(HCRYPTPROV *phProv,
                                           LPCWSTR szContainer, DWORD dwFlags,
                                           void *pVTable);
ANEM_CSP_API BOOL WINAPI CPReleaseContext(HCRYPTPROV hProv, DWORD dwFlags);
ANEM_CSP_API BOOL WINAPI CPGenKey(HCRYPTPROV hProv, ALG_ID Algid,
                                  DWORD dwFlags, HCRYPTKEY *phKey);
ANEM_CSP_API BOOL WINAPI CPDeriveKey(HCRYPTPROV hProv, ALG_ID Algid,
                                     HCRYPTHASH hHash, DWORD dwFlags,
                                     HCRYPTKEY *phKey);
ANEM_CSP_API BOOL WINAPI CPDestroyKey(HCRYPTPROV hProv, HCRYPTKEY hKey);
ANEM_CSP_API BOOL WINAPI CPSetKeyParam(HCRYPTPROV hProv, HCRYPTKEY hKey,
                                       DWORD dwParam, const BYTE *pbData,
                                       DWORD dwFlags);
ANEM_CSP_API BOOL WINAPI CPGetKeyParam(HCRYPTPROV hProv, HCRYPTKEY hKey,
                                       DWORD dwParam, LPBYTE pbData,
                                       LPDWORD pcbDataLen, DWORD dwFlags);
ANEM_CSP_API BOOL WINAPI CPSetProvParam(HCRYPTPROV hProv, DWORD dwParam,
                                        const BYTE *pbData, DWORD dwFlags);
ANEM_CSP_API BOOL WINAPI CPGetProvParam(HCRYPTPROV hProv, DWORD dwParam,
                                        LPBYTE pbData, LPDWORD pcbDataLen,
                                        DWORD dwFlags);
ANEM_CSP_API BOOL WINAPI CPSetHashParam(HCRYPTPROV hProv, HCRYPTHASH hHash,
                                        DWORD dwParam, const BYTE *pbData,
                                        DWORD dwFlags);
ANEM_CSP_API BOOL WINAPI CPGetHashParam(HCRYPTPROV hProv, HCRYPTHASH hHash,
                                        DWORD dwParam, LPBYTE pbData,
                                        LPDWORD pcbDataLen, DWORD dwFlags);
ANEM_CSP_API BOOL WINAPI CPExportKey(HCRYPTPROV hProv, HCRYPTKEY hKey,
                                     HCRYPTKEY hPubKey, DWORD dwBlobType,
                                     DWORD dwFlags, LPBYTE pbData,
                                     LPDWORD pcbDataLen);
ANEM_CSP_API BOOL WINAPI CPImportKey(HCRYPTPROV hProv, const BYTE *pbData,
                                     DWORD cbDataLen, HCRYPTKEY hPubKey,
                                     DWORD dwFlags, HCRYPTKEY *phKey);
ANEM_CSP_API BOOL WINAPI CPEncrypt(HCRYPTPROV hProv, HCRYPTKEY hKey,
                                   HCRYPTHASH hHash, BOOL fFinal,
                                   DWORD dwFlags, LPBYTE pbData,
                                   LPDWORD pcbDataLen, DWORD cbBufLen);
ANEM_CSP_API BOOL WINAPI CPDecrypt(HCRYPTPROV hProv, HCRYPTKEY hKey,
                                   HCRYPTHASH hHash, BOOL fFinal,
                                   DWORD dwFlags, LPBYTE pbData,
                                   LPDWORD pcbDataLen);
ANEM_CSP_API BOOL WINAPI CPCreateHash(HCRYPTPROV hProv, ALG_ID Algid,
                                      HCRYPTKEY hKey, DWORD dwFlags,
                                      HCRYPTHASH *phHash);
ANEM_CSP_API BOOL WINAPI CPHashData(HCRYPTPROV hProv, HCRYPTHASH hHash,
                                    const BYTE *pbData, DWORD cbDataLen,
                                    DWORD dwFlags);
ANEM_CSP_API BOOL WINAPI CPHashSessionKey(HCRYPTPROV hProv, HCRYPTHASH hHash,
                                          HCRYPTKEY hKey, DWORD dwFlags);
ANEM_CSP_API BOOL WINAPI CPSignHash(HCRYPTPROV hProv, HCRYPTHASH hHash,
                                    DWORD dwKeySpec, LPCWSTR szDescription,
                                    DWORD dwFlags, LPBYTE pbSignature,
                                    LPDWORD pcbSigLen);
ANEM_CSP_API BOOL WINAPI CPDestroyHash(HCRYPTPROV hProv, HCRYPTHASH hHash);
ANEM_CSP_API BOOL WINAPI CPVerifySignature(HCRYPTPROV hProv,
                                           HCRYPTHASH hHash,
                                           const BYTE *pbSignature,
                                           DWORD cbSigLen, HCRYPTKEY hPubKey,
                                           LPCWSTR szDescription,
                                           DWORD dwFlags);
ANEM_CSP_API BOOL WINAPI CPGenRandom(HCRYPTPROV hProv, DWORD cbLen,
                                     LPBYTE pbBuffer);
ANEM_CSP_API BOOL WINAPI CPGetUserKey(HCRYPTPROV hProv, DWORD dwKeySpec,
                                      HCRYPTKEY *phUserKey);
ANEM_CSP_API BOOL WINAPI CPDuplicateHash(HCRYPTPROV hProv, HCRYPTHASH hHash,
                                         LPDWORD pdwReserved, DWORD dwFlags,
                                         HCRYPTHASH *phHash);
ANEM_CSP_API BOOL WINAPI CPDuplicateKey(HCRYPTPROV hProv, HCRYPTKEY hKey,
                                        LPDWORD pdwReserved, DWORD dwFlags,
                                        HCRYPTKEY *phKey);

#ifdef __cplusplus
}
#endif

#endif /* ANEMOS_CSP_ABI_H */
