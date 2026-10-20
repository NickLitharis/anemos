# P3 crypto: the engine is built, the seam is next

P3 is "an OpenSSL-backed CSP in the empty `rsaenh` slot". This document records
the groundwork that is done and the two hard facts discovered while doing it —
one of which changes what "pass XP's self-tests" can mean.

No Microsoft source is used as implementation. `Source/` was read only to
establish the provider ABI and the loader's behaviour.

## What is done

`anemos/crypto/` now has:

- `build-openssl.sh` — a reproducible cross-build of **OpenSSL 3.5.7** for
  `i686-w64-mingw32` with the pinned clang 23.1.3, static, `no-asm`. Target
  name is `mingw`; OpenSSL 3.x no longer has `mingw32`, and passing it makes
  Configure print the target list and exit 1. Installed at
  `~/.local/opt/openssl-3.5.7-i686` (outside the repo, like the toolchain).
- `kat.c` — known-answer tests for the algorithms the CSP must expose, linked
  statically against that library and run as a 32-bit PE under Wine.
- `run.sh` — 6 checks, all able to fail.

Evidence (`anemos/crypto/run.sh`, 6/6):

```
OpenSSL: OpenSSL 3.5.7 9 Jun 2026
PASS sha1-abc
PASS sha256-abc
PASS sha512-abc
PASS hmac-sha256
PASS aes-128-cbc
PASS aes-256-gcm ciphertext
PASS aes-256-gcm tag
PASS rsa-1024 sign/verify
PASS rsa-1024 tampered signature is rejected
```

Vectors are FIPS 180 (SHA-1/256/512), RFC 4231 case 1 (HMAC-SHA-256), NIST
SP 800-38A F.2.1 (AES-128-CBC, padding off) and the GCM all-zero 256-bit key /
96-bit IV / 128-bit plaintext case. The negative control compiles the same
suite with a deliberately wrong SHA-256 expectation and requires it to fail.
The binary links the library statically — no DLL dependency for the guest.

This proves the engine works **for the destination ABI**, which was the open
question. It does not prove the CSP around it.

## The provider surface, measured

`advapi32/cryptapi.c` is the authoritative loader. It calls `LoadLibrary` and
then `GetProcAddress` for an exact list. A provider missing any of these fails
with `NTE_PROVIDER_DLL_FAIL`:

Required (23):
`CPAcquireContext`, `CPReleaseContext`, `CPGenKey`, `CPDeriveKey`,
`CPDestroyKey`, `CPSetKeyParam`, `CPGetKeyParam`, `CPExportKey`,
`CPImportKey`, `CPEncrypt`, `CPDecrypt`, `CPCreateHash`, `CPHashData`,
`CPHashSessionKey`, `CPDestroyHash`, `CPSignHash`, `CPVerifySignature`,
`CPGenRandom`, `CPGetUserKey`, `CPSetProvParam`, `CPGetProvParam`,
`CPSetHashParam`, `CPGetHashParam`.

Optional (2): `CPDuplicateKey`, `CPDuplicateHash`.

`public/sdk/inc/cspdk.h` has 26 prototypes — the 25 above plus
`CPAcquireContextW`. All are `WINAPI` (`__stdcall`). Existing in-tree
providers export by name through a `.def` file (see
`ds/security/csps/cryptoflex/slbcsp/slbcsp.def`), with `DllRegisterServer` /
`DllUnregisterServer` private.

## Blocker: XP will not load an unsigned provider

This is the important discovery. `CryptAcquireContext` verifies the provider
image before `CPAcquireContext` is ever called:

- if the DLL has no signature resource, it reads a `Signature` value from the
  provider's registry key; if neither exists → `NTE_BAD_SIGNATURE`
  (`advapi32/cryptapi.c`, ~lines 1030-1073);
- `NewVerifyImage` hashes the file with MD5 and checks the signature against
  keys compiled into `advapi32` (`CheckAllSignatures`).

There is no private key for those Microsoft keys, so a new provider cannot pass
this check on stock XP. **No `CRYPT_VERIFYCONTEXT` bypass appears in this
path**; the flag's call sites in the tree are callers asking for an ephemeral
context, not the loader skipping verification. Whether the kernel-side loader
honours the flag differently is an open question and needs a guest experiment,
not an assumption.

Consequences for the P3 gate:

1. The CSP's **SPI** can be verified in XP by loading the DLL with
   `LoadLibrary` and calling the `CP*` entry points directly. That is a real
   gate for the implementation and the ABI, and it is not blocked by signing.
2. The **CryptoAPI integration** gate ("XP's own crypto self-tests" through
   `CryptAcquireContext`) cannot use an unsigned DLL on an unmodified XP. The
   honest options are:
   - prove the SPI directly (recommended first);
   - test through CryptoAPI in a *separately labelled harness image* whose
     `advapi32` skips the signature check — this validates plumbing, but the
     modification is part of the harness, not the deliverable, and must be
     stated as such;
   - establish by experiment whether any supported flag or registry policy
     bypasses verification on this XP build. If it does, use it; if not, say so.

This does not make P3 impossible. It changes the claim: we can prove the
provider's behaviour, and we can prove CryptoAPI plumbing under a declared
harness change, but we cannot make stock XP trust a third-party signature.

## Second hazard: RAND recursion

OpenSSL's Windows RAND source uses CryptoAPI (`CryptAcquireContext` /
`CryptGenRandom`) to seed. If `rsaenh.dll` *is* our provider and OpenSSL's RAND
calls CryptoAPI, the call re-enters the provider: unbounded recursion, likely
at the first key generation or signature.

Mitigation to implement with the CSP:
- build/configure OpenSSL's seed source explicitly, and seed the DRBG from
  `SystemFunction036` (`RtlGenRandom`), which is independent of the CSP; or
- build with `--with-rand-seed=none` and seed explicitly at provider load.

This must be a test: a KAT that generates an RSA key **with the CSP loaded as
rsaenh** and still returns. A host-side KAT cannot catch this recursion.

## Staged plan

| Step | Work | Gate |
|---|---|---|
| P3.0 | OpenSSL cross-build + KATs | ✅ 6/6, this document |
| P3.1 | CSP skeleton exporting all 23+2 names; acquire/release; hash objects; `CPGenRandom`; property queries | a host test that `LoadLibrary`s and calls every entry point |
| P3.2 | Symmetric keys: `CPGenKey`, `CPDeriveKey`, `CPEncrypt/Decrypt`, import/export | known-answer tests through the SPI |
| P3.3 | RSA: key import/export, `CPSignHash`, `CPVerifySignature`, `CPGetUserKey` | sign/verify and negative controls through the SPI |
| P3.4 | Run the same tests inside XP via `LoadLibrary` | identical results to the host run |
| P3.5 | CryptoAPI integration | `CryptAcquireContext` path, under a documented harness decision about signing |
| P3.6 | RAND recursion test with the DLL in the `rsaenh` slot | key generation succeeds and does not recurse |

## Reproducing

```sh
anemos/crypto/build-openssl.sh    # once; builds 3.5.7 for i686 Windows
anemos/crypto/run.sh              # 6/6, no VM
```

The library lives outside the repo at `~/.local/opt/openssl-3.5.7-i686`, like
the pinned LLVM toolchain. `run.sh` fails with a clear message if it is absent.
