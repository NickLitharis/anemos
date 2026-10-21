# P3.1/P3.2a CSP: skeleton, hashing, and AES keys

`anemos/csp/` is the OpenSSL-backed provider: a 32-bit DLL that exports the
complete CryptoSPI surface, implements contexts, hash objects, randomness,
provider properties and AES key objects with streaming encryption, and is
exercised by a harness that loads it directly.

The engine is the pinned OpenSSL 3.5.7 build from `anemos/crypto/`. No
Microsoft source is used; `csp_abi.h` declares the provider surface from the
API specification, and the platform types come from the SDK headers.

## What is implemented

| Area | Entry points | Behaviour |
|---|---|---|
| Context | `CPAcquireContext`, `CPAcquireContextW`, `CPReleaseContext` | 32 slots, container name recorded, `CRYPT_DELETEKEYSET` refused, release destroys the context's hashes and keys |
| Randomness | `CPGenRandom` | `SystemFunction036` (`RtlGenRandom`), **not** OpenSSL's CryptoAPI RAND — that would re-enter the provider when installed as `rsaenh.dll` |
| Hashing | `CPCreateHash`, `CPHashData`, `CPGetHashParam`, `CPDestroyHash`, `CPDuplicateHash` | MD5, SHA-1, SHA-256 via OpenSSL EVP; `HP_HASHVAL` finalises a copy, so repeated reads work; `HP_HASHSIZE`; two-call size queries return `ERROR_MORE_DATA` |
| Provider | `CPGetProvParam`, `CPSetProvParam` | `PP_NAME`, `PP_VERSION` (2.0), `PP_PROVTYPE` (`PROV_RSA_FULL`), `PP_IMPTYPE` (`CRYPT_IMPL_SOFTWARE`), `PP_ENUMALGS` (MD5, SHA1, SHA256) |
| Keys | `CPGenKey`, `CPDeriveKey`, `CPImportKey`, `CPExportKey`, `CPDestroyKey`, `CPDuplicateKey`, `CPGetKeyParam`, `CPSetKeyParam` | AES-128/192/256; `PLAINTEXTKEYBLOB`; CBC/ECB; PKCS#5 or zero padding; `KP_ALGID`, `KP_KEYLEN`, `KP_BLOCKLEN`, `KP_MODE`, `KP_PADDING`, `KP_IV`; export only when `CRYPT_EXPORTABLE` |
| Cipher | `CPEncrypt`, `CPDecrypt` | multi-call streaming with OpenSSL's block buffering; one-shot is the final call; PKCS#5 or zero padding; per-key stream state |
| Hash session key | `CPHashSessionKey` | feeds the key bytes into an existing hash |
| RSA | `CPGenKey` (`CALG_RSA_SIGN`/`CALG_RSA_KEYX`), `CPGetUserKey`, `CPSignHash`, `CPVerifySignature`, `CPImportKey`/`CPExportKey` | 1024-bit key pairs; `PUBLICKEYBLOB`/`PRIVATEKEYBLOB`; PKCS#1 v1.5 sign/verify over MD5/SHA-1/SHA-256; ephemeral user key per spec |
| Not implemented | raw RSA `CPEncrypt`/`CPDecrypt`, keyset persistence | explicit `NTE_NOT_SUPPORTED`/`NTE_INVALID_PARAMETER` |

The 23 required and 2 optional names from `advapi32`'s loader table are all
exported **undecorated** (`--kill-at`), so `GetProcAddress` finds them. The
optional `CPAcquireContextW` is exported too.

Handles are small indexed values with range checks (`0x10000+` contexts,
`0x20000+` hashes, `0x30000+` keys). The provider has no hidden allocation
policy question: it is user-mode, and all state is in fixed tables guarded by a
`CRITICAL_SECTION`. No CRT object crosses the provider boundary.

## Verification

`anemos/csp/run.sh` — 11 checks, all able to fail:

1. the pinned clang builds `anemosp.dll` against the cross-built OpenSSL;
2. all 25 SPI names are present and undecorated in the export table;
3. every import in the DLL resolves on XP. The export lists are extracted from
   `xp-pristine.vmdk`, and a synthetic `InitializeSRWLock` binary must be
   rejected by the same check. This gate is why the engine is built with
   `no-threads`; see `docs/P3-CRYPTO.md`;
4. the harness (a 32-bit PE) `LoadLibrary`s the DLL and passes every check
   under Wine — and its own imports are checked against XP too:
   - contexts acquire/release, including a second container;
   - `CPGenRandom` returns distinct non-zero buffers;
   - MD5("abc"), SHA-1("abc"), SHA-256("abc") match published answers through
     `CPCreateHash`/`CPHashData`/`CPGetHashParam`, including the two-call size
     pattern and `CPDuplicateHash`;
   - provider properties answer correctly, including `PP_ENUMALGS` ordering and
     `NTE_NO_MORE_ITEMS`;
   - AES-128/192/256 known-answer vectors through the SPI (SP 800-38A CBC and
     ECB), PKCS#5 and zero padding, `KP_*` parameters, `PLAINTEXTKEYBLOB`
     import/export/duplicate, hash-derived keys, and negative cases: wrong blob
     length, non-exportable export, short encrypt buffer, tampered padding,
     invalid mode and unsupported algorithm;
   - multi-call streaming: 5+11+16 byte encryption reproduces the two-block
     SP 800-38A ciphertext, 7+9+16 byte decryption reproduces the plaintext,
     PKCS#5 streaming round-trips, a partial zero-padded final is rejected, and
     a second one-shot after `fFinal` restarts from the configured IV;
   - RSA: user-key generation and stability, PUBLICKEYBLOB/PRIVATEKEYBLOB
     export/import (including private round-trip), SHA-256 sign/verify,
     tampered and wrong-digest rejection, private-handle verification, and
     refusal to export a non-exportable private key;
   - unsupported asymmetric operations fail with `NTE_NOT_SUPPORTED`, unknown
     algorithms with `NTE_BAD_ALGID`, invalid handles with `NTE_BAD_HASH` or
     `NTE_BAD_KEY`;
5. a harness built with a wrong expected SHA-1 digest must fail.

The harness calls the SPI **directly**. It does not use `CryptAcquireContext`,
because stock XP verifies the provider image signature before calling
`CPAcquireContext` (see `docs/P3-CRYPTO.md`). That constraint is unchanged; this
gate is about the implementation and the ABI, not about Microsoft trusting the
DLL.

Artifact: `anemosp.dll` is about 3.6 MB, almost all statically linked OpenSSL.
Size work comes after the feature set; the next slices add streaming and RSA.

Because OpenSSL is built without its own threading, the provider serialises
every OpenSSL call through one `CRITICAL_SECTION`. That is the price of an
XP-clean import table, and it is explicit in the code.

## Limits and next

- **Streaming state is per key.** A direction switch or a mode/IV/padding
  change resets it and discards buffered bytes; `fFinal` resets to the
  configured IV. A key cannot be used by two concurrent streams.
- **Zero padding requires block-aligned input** across the stream; a partial
  final returns failure. PKCS#5 accepts arbitrary lengths.
- **Key derivation is explicit, not RSA-style.** `CPDeriveKey` takes the first
  `key_len` bytes of the hash value; the hash must be at least that long.
- **No persistent keysets.** `CPGetUserKey` generates an ephemeral key per
  provider and key spec; it dies with the context.
- **RSA is 1024-bit and PKCS#1 v1.5 only.** Import accepts up to 16384-bit
  keys; generation is fixed at 1024. No PSS, no raw RSA encrypt/decrypt.
- **32 live objects per kind.** A context limit is a real provider limit; a
  caller using more gets `NTE_NO_MEMORY`.
- **No keyed hashes (HMAC).** `CPCreateHash` with a key handle is refused;
  `CPHashSessionKey` hashes key bytes into an existing hash.
- **Provider properties are a subset.** `PP_ENUMALGS_EX`, `PP_KEYSET_SEC_DESCR`,
  UI callbacks and container enumeration are not implemented.
- **No signature.** The DLL cannot be loaded through CryptoAPI on stock XP.
  Testing that path requires a documented harness change; the SPI is proved
  directly here.

## P3.4 — the same checks inside XP

`anemos/csp/xp_run.sh` carries the host artifact into the guest the same way the
STATUS→DOS probe travels: host-side victim swap, boot, Run box, host-side
collection.

- **Two victims**, because `ntfspatch.py` can only overwrite an existing file:
  the harness (~218 KB) over `mspaint.exe`, and the provider (~3.6 MB) over
  `\windows\repair\software`. Both are backed up before the swap and restored on
  every exit path. Candidates are filtered by `ntfspatch.py stat` for size and
  for NTFS compression — a raw write into a compressed file reads back as
  garbage, so such a file must never be chosen.
- **No boot-critical victim.** The provider does not go over `shell32.dll` or
  `oembios.bin` while a safe 8 MB file exists; the recovery-hive copy in
  `\windows\repair` is not read during a normal boot.
- **The harness writes a result file.** A second argument makes `spi_test`
  mirror every PASS/FAIL line to `\conftest\spi.log`; in guest mode it also
  holds its console for 8 s so the runner's pixel probe can observe the launch
  before the process exits.
- **The gate is the diff.** The runner types
  `mspaint.exe \windows\repair\software \conftest\spi.log` into the Run box
  (no `:` or `%` travels), waits for the console to close, powers off, and
  extracts the log. The guest PASS/FAIL lines must be identical to the host
  run's, and the guest must end with `all CSP SPI checks pass`.

Status: the harness is prepared and its host half is green; the guest run is
recorded only when it actually passes.

## Reproducing

```sh
anemos/crypto/build-openssl.sh   # once, if the engine is not built
anemos/csp/run.sh                # 11/11, no VM
anemos/csp/xp_run.sh             # P3.4: the same checks inside XP
```
