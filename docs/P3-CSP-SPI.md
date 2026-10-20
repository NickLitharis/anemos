# P3.1 CSP: the SPI skeleton, proved without CryptoAPI

`anemos/csp/` is the first slice of the OpenSSL-backed provider: a 32-bit
provider DLL that exports the complete CryptoSPI surface, implements contexts,
hash objects, randomness and provider properties, and is exercised by a
harness that loads it directly.

The engine is the pinned OpenSSL 3.5.7 build from `anemos/crypto/`. No
Microsoft source is used; `csp_abi.h` declares the provider surface from the
API specification, and the platform types come from the SDK headers.

## What is implemented

| Area | Entry points | Behaviour |
|---|---|---|
| Context | `CPAcquireContext`, `CPAcquireContextW`, `CPReleaseContext` | 32 slots, container name recorded, `CRYPT_DELETEKEYSET` refused, release destroys the context's hashes |
| Randomness | `CPGenRandom` | `SystemFunction036` (`RtlGenRandom`), **not** OpenSSL's CryptoAPI RAND — that would re-enter the provider when installed as `rsaenh.dll` |
| Hashing | `CPCreateHash`, `CPHashData`, `CPGetHashParam`, `CPDestroyHash`, `CPDuplicateHash` | MD5, SHA-1, SHA-256 via OpenSSL EVP; `HP_HASHVAL` finalises a copy, so repeated reads work; `HP_HASHSIZE`; two-call size queries return `ERROR_MORE_DATA` |
| Provider | `CPGetProvParam`, `CPSetProvParam` | `PP_NAME`, `PP_VERSION` (2.0), `PP_PROVTYPE` (`PROV_RSA_FULL`), `PP_IMPTYPE` (`CRYPT_IMPL_SOFTWARE`), `PP_ENUMALGS` (MD5, SHA1, SHA256) |
| Everything else | 14 key/asymmetric entry points | explicit stubs: `FALSE` + `NTE_NOT_SUPPORTED` |

The 23 required and 2 optional names from `advapi32`'s loader table are all
exported **undecorated** (`--kill-at`), so `GetProcAddress` finds them. The
optional `CPAcquireContextW` is exported too.

Handles are small indexed values with range checks (`0x10000+` contexts,
`0x20000+` hashes). The provider has no hidden allocation policy question: it
is user-mode, and all state is in fixed tables guarded by a
`CRITICAL_SECTION`. No CRT object crosses the provider boundary.

## Verification

`anemos/csp/run.sh` — 7 checks, all able to fail:

1. the pinned clang builds `anemosp.dll` against the cross-built OpenSSL;
2. all 25 SPI names are present and undecorated in the export table;
3. the harness (a 32-bit PE) `LoadLibrary`s the DLL and passes every check
   under Wine:
   - contexts acquire/release, including a second container;
   - `CPGenRandom` returns distinct non-zero buffers;
   - MD5("abc"), SHA-1("abc"), SHA-256("abc") match published answers through
     `CPCreateHash`/`CPHashData`/`CPGetHashParam`, including the two-call size
     pattern and `CPDuplicateHash`;
   - provider properties answer correctly, including `PP_ENUMALGS` ordering and
     `NTE_NO_MORE_ITEMS`;
   - unsupported key operations fail with `NTE_NOT_SUPPORTED`, unknown
     algorithms with `NTE_BAD_ALGID`, invalid handles with `NTE_BAD_HASH`;
4. a harness built with a wrong expected SHA-1 digest must fail.

The harness calls the SPI **directly**. It does not use `CryptAcquireContext`,
because stock XP verifies the provider image signature before calling
`CPAcquireContext` (see `docs/P3-CRYPTO.md`). That constraint is unchanged; this
gate is about the implementation and the ABI, not about Microsoft trusting the
DLL.

Artifact: `anemosp.dll` is 3.6 MB, almost all statically linked OpenSSL. Size
work comes after the feature set; the next slices add keys and RSA.

## Limits and next

- **Key objects are not implemented.** `CPGenKey`, `CPDeriveKey`,
  `CPImportKey`/`CPExportKey`, `CPEncrypt`/`CPDecrypt`, `CPSignHash`/
  `CPVerifySignature`, `CPGetUserKey`, key params and `CPHashSessionKey` are
  explicit `NTE_NOT_SUPPORTED` stubs. P3.2 (symmetric) and P3.3 (RSA) fill
  them.
- **32 live objects per kind.** A context limit is a real provider limit; a
  caller using more gets `NTE_NO_MEMORY`.
- **No keyed hashes.** `CPCreateHash` with a key handle is refused; HMAC is
  part of the key slice.
- **Provider properties are a subset.** `PP_ENUMALGS_EX`, `PP_KEYSET_SEC_DESCR`,
  UI callbacks and container enumeration are not implemented.
- **No signature.** The DLL cannot be loaded through CryptoAPI on stock XP.
  Testing that path requires a documented harness change; the SPI is proved
  directly here.

## Reproducing

```sh
anemos/crypto/build-openssl.sh   # once, if the engine is not built
anemos/csp/run.sh                # 7/7, no VM
```
