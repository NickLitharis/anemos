# Anemos — the plan

**This document supersedes `MODERNIZATION_PLAN.md`, `docs/OPEN-REPLACEMENT-PLAN.md` and
`docs/ANEMOS-PLAN.md`.** Those are kept for history; where they disagree with this, this
wins. They used three incompatible numbering schemes (M0-M4, R0-R5, P0-P5) for the same
work, which is a reliable way to lose track of what is done.

---

## The goal, stated once

Build an operating system inspired by Windows XP: **new code, written to 2026 standards,
validated for behavioural compatibility against XP itself.**

Three constraints follow, and they are not negotiable:

1. **Legal.** No Microsoft source in the deliverable. The leak is a specification and a
   test oracle only (`NOTICE.md`).
2. **Standards.** C23 / C++23, clang 23.1.3, `docs/OS-CODING-RULES.md` (17 rules),
   `docs/SIZE-POLICY.md`.
3. **Compatibility.** XP is the oracle. Where our behaviour differs, XP is right.

---

## Where we actually are

Not a roadmap — measured state as of today.

| Capability | Status | Evidence |
|---|---|---|
| Build XP's kernel from source | ✅ | 59 libraries, 1,464 exports matching Microsoft's |
| Boot it into a full XP install | ✅ | `docs/KERNEL-BOOTS-XP.md` |
| Build 21/23 kernel subsystems for **AMD64** | ✅ | `docs/X64-STATUS.md` |
| Modern compiler → legacy libs → working PE | ✅ | clang 23.1.3 + `lld-link` on real `ntosrtl.lib` |
| Two bootable kernels, one disk (the oracle) | ✅ | `xp-pristine.vmdk` / `xp.vmdk` |
| Host-side binary swap, no root | ✅ | `tools/ntfspatch.py` |
| Legal boundary | ✅ | `LICENSE`, `NOTICE.md`, `SCOPE.md` |
| Coding standards | ✅ | `docs/OS-CODING-RULES.md` |
| **Conformance runner** | ✅ | `harness/conformance/runner.py` → `docs/G2-CONFORMANCE-RUNNER.md` (IDENTICAL, A/A and A/B) |
| **ABI compatibility proof** | ✅ | `tools/abi/run.sh` → `docs/G1-ABI-PROOF.md` (15/15) |
| **Anemos code — P1** | ✅ **6/6** | `anemos/{md5,sha1,base64,crc32,nls,statusdos}/` — RFC 1321 + FIPS 180-1 + RFC 4648 + zlib + CP1252/RFC 3629 vectors, plus the `STATUS→DOS` map verified against live XP for **all 2^32 statuses** (`docs/P1-STATUSDOS.md`) |
| **Anemos code — P2 DNS codec** | ✅ | `anemos/dns/` — RFC 1035 parse/serialise, byte-identical canonical round-trip, 1,365-case differential corpus against an independent Python implementation (`docs/P2-DNS.md`) |
| **Anemos code — P2 DNS client** | ✅ | `anemos/dns/dns_client.*` — response correlation, RCODE/TC, case-insensitive matching, bounded CNAME chains; 2,040-case differential corpus, 24/24 with the codec (`docs/P2-DNS-CLIENT.md`) |
| **Anemos code — P2 DHCP codec** | ✅ | `anemos/dhcp/` — RFC 2131/2132 parse/serialise/builder, exact round-trip for every accepted message, 4,095-case differential corpus (`docs/P2-DHCP.md`) |
| **Anemos code — P2 DHCP client** | ✅ | `anemos/dhcp/dhcp_client.*` — socket-free state machine, T1/T2/expiry timers, scenario suite + 6-trace/130-operation differential, 24/24 with the codec (`docs/P2-DHCP-CLIENT.md`) |
| **P3 groundwork — crypto engine** | ✅ | OpenSSL 3.5.7 cross-built for i686 Windows (no-threads/no-async/thread-pool, XP-clean import surface); SHA-1/256/512, HMAC, AES-CBC/GCM and RSA KATs pass, 7/7, imports checked against `xp-pristine.vmdk` (`docs/P3-CRYPTO.md`) |
| **P3.1 — CSP SPI skeleton** | ✅ | `anemos/csp/` — all 23 required + 2 optional `CP*` exports, contexts, MD5/SHA-1/SHA-256, `CPGenRandom` via `SystemFunction036`, provider params; XP import gate + LoadLibrary harness 11/11 (`docs/P3-CSP-SPI.md`) |
| **P3.2a — symmetric keys + AES** | ✅ | AES-128/192/256 key objects, CBC/ECB, PKCS#5/zero padding, `PLAINTEXTKEYBLOB` import/export/duplicate, `CPDeriveKey`, one-shot `CPEncrypt`/`CPDecrypt`; SP 800-38A KATs through the SPI, 11/11 (`docs/P3-CSP-SPI.md`) |
| **P3.2b — streaming cipher** | ✅ | Multi-call `CPEncrypt`/`CPDecrypt` with per-key EVP buffering; 5+11+16-byte encryption reproduces SP 800-38A, PKCS#5 and zero-padding streaming tests, 11/11 (`docs/P3-CSP-SPI.md`) |
| **P3.3 — RSA keys + sign/verify** | ✅ | 1024-bit `CALG_RSA_SIGN`/`KEYX`, `PUBLICKEYBLOB`/`PRIVATEKEYBLOB` export/import (private round-trip), ephemeral `CPGetUserKey`, PKCS#1 v1.5 `CPSignHash`/`CPVerifySignature`; tamper/wrong-digest negatives, 11/11 (`docs/P3-CSP-SPI.md`) |
| **P4.1 — Winsock address layer** | ✅ | Byte order, `sockaddr_in`, strict IPv4 `inet_pton`/`inet_ntop`/`inet_addr`/`inet_ntoa`; 586-case corpus against Python `ipaddress`, negative control rejects a wrong renderer; `addr.c` 438 B `.text` (`docs/P4-NETWORK.md`). POSIX behaviour claimed, XP quirks deferred to the P4.6 probe. |
| **P4.5 — transport seam + loopback** | ✅ reference | `open/bind/listen/connect/accept/send/shutdown/close/refuse` requests with injected events; 27 end-to-end assertions over TCP, UDP, EOF, refusal and backlog, negative byte-drop control; `loopback.c` 1061 B `.text` (`docs/P4-NETWORK.md`). The real TDI/WSK or adopted-stack provider remains the open decision. |
| **P4.4 — resolver over the socket path** | ✅ | DNS A/AAAA over the socket core: explicit-time retries, in-message CNAME chains, fresh xid per restart, 18 differential traces (ok/dns/timeout all exercised), negative control on every ok trace; `resolver.c` 914 B `.text` (`docs/P4-NETWORK.md`). Closes P2's deferred socket path. |
| **P4.3 — readiness and data path** | ✅ | `select` over read/write sets, non-blocking `send`/`recv`, EOF/reset, `WSAEWOULDBLOCK`/`WSAESHUTDOWN`/`WSAECONNRESET`; 40 traces match the model, 30k-op fuzz clean, `socket_core.c` 2269 B `.text` (`docs/P4-NETWORK.md`). No blocking wait — the transport owns that. |
| **P4.2 — socket lifecycle core** | ✅ | Transport-free state machine for `socket`/`bind`/`listen`/`connect`/`accept`/`shutdown`/`close`, with a drained request queue and injected events; 26 traces match an independent Python model, a broken accept queue is rejected, `socket_core.c` 1261 B `.text` (`docs/P4-NETWORK.md`). |
| **P3.5 — CryptoAPI plumbing** | ✅ Wine | Registered in a Wine prefix and driven through advapi32: acquire, hashes, random, AES-CBC vector, RSA sign/verify, plus a cross-provider check proving the little-endian CryptoAPI signature convention; 7/7 (`docs/P3-CSP-SPI.md`). Stock XP's image-signature check still blocks the real XP path. |

The method is proven, P1 is written and gated
(`anemos/{md5,sha1,base64,crc32,nls,statusdos}/`; shared status codes in
`anemos/include/`), and P2's DNS codec/client, DHCP codec and DHCP client are
gated (`anemos/{dns,dhcp}/`). Everything from here is substitution work behind
stable interfaces.

---

## The two gates

Everything else waits behind these. Both are small, both are cheap, and skipping either
means building on sand.

### G1 — ABI compatibility proof  *(~1 day)* — **PASSED 2026-10-18**

**Status:** 15/15 checks, clang 23.1.3 vs the tree's MSVC 13.00, i386. Layout,
decoration, cross-calls in both directions, and a clang object linked against real
`ntosrtl.lib` — plus a negative control on the diff. See `docs/G1-ABI-PROOF.md`,
reproduce with `tools/abi/run.sh`. AMD64 is not covered by this proof.

**Question:** do clang 23 and the tree's MSVC 13.00 agree on struct layout, calling
conventions and name mangling?

**Method:** one clang-built C23 object exporting a `__stdcall` function taking a packed
struct; link against a real kernel library; call it from something that already builds;
compare every field offset against the MSVC-computed values.

**Gate:** offsets and call ABI match exactly. **If this fails, nothing downstream is
trustworthy** — every replacement would silently corrupt structures.

This is the highest-risk item in the entire project and it costs one day. Do it first.

### G2 — Conformance runner  *(~1 week)* — **PASSED 2026-10-18**

**Status:** one VM, two disks, keyboard-driven probe inside XP, host-side extraction
through `tools/ntfspatch.py`. Reports **IDENTICAL** for the same disk twice (stability)
and for `xp-pristine.vmdk` vs `xp.vmdk` (the gate); both of its failure directions were
demonstrated (one changed guest-file byte, one flipped console pixel). See
`docs/G2-CONFORMANCE-RUNNER.md`; reproduce with the commands in that document.

**Question:** can we prove a replacement behaves like XP?

**Method:** `harness/` gains the ability to boot a VM, install a subject binary, run N
tests inside XP, collect observations (return codes, packets, registry state, screenshots),
and diff against the control.

**Gate:** the runner, pointed at `xp.vmdk` and `xp-pristine.vmdk` — **the same kernel data,
two disk images** — must report **identical**. If it reports a difference, the runner is
wrong, not the kernels. That negative control is what makes it trustworthy.

**This is the real work of the project.** Writing a DNS codec is a few days; knowing it is
*correct* is the hard part, and it is the thing that outlives any implementation.

---

## Phases

Each phase begins only when the previous gate passes.

| Phase | Work | Gate |
|---|---|---|
| **P0** | G1, G2 | ABI matches; runner reports identical on the negative control |
| **P1** | First replacement: pure algorithms — CRC-32, MD5/SHA, Base64, `STATUS→DOS` map, NLS tables | byte-identical on NIST/RFC vectors; `STATUS→DOS` proved against XP for all 2^32 statuses by an in-guest certificate with zero mismatches (`anemos/statusdos/run.sh`, 14/14); negative controls must fail |
| **P2** | Wire formats: DNS codec/client (**done**), DHCP codec/client (**done**) | identical bytes out, identical error codes on malformed input |
| **P3** | Crypto: engine (**done, XP-safe**), SPI skeleton (**done**), AES keys/streaming (**done**), RSA sign/verify (**done**), then the XP guest run and CryptoAPI path | known-answer tests through the SPI; CryptoAPI integration is constrained by XP's provider-signature check — see `docs/P3-CRYPTO.md` |
| **P4** | Network stack (288 C files) — largest, deliberately last. **Started 2026-10-21** with the Winsock address layer (P4.1), the socket lifecycle core (P4.2) the readiness/data path (P4.3) the resolver glue (P4.4) and the transport seam with a loopback reference (P4.5); the stack architecture (lwIP-style vs ReactOS behind TDI/WSK) is still open — `docs/P4-NETWORK.md` | Winsock conformance; throughput not worse |
| **P5** | Kernel proper | **Out of scope.** See below. |

P1 comes before P2 because it needs no VM, so the runner gets exercised cheaply before it
is asked to judge anything hard.

---

## What is deliberately out of scope

**win32k and ntgdi.** Rendering *is* the appearance guarantee. A different rasteriser
means different pixels, and "identical looks" is a stated goal. GDI stays.

**The kernel proper (P5).** Memory manager, object manager, scheduler, I/O — ~60M lines
with **no external specification** to implement against and no way to differential-test
bit-identical internal state. The honest options are adopting ReactOS's clean-room kernel
(GPL/LGPL, which changes this project's licence) or accepting that layer stays Microsoft's.
Decide after P2 proves the method, not now.

By line count, P1–P4 are roughly **5% of NT**. They are also the 5% that has published
standards. That is the honest scope of "an open OS": a well-specified shell over a kernel
nobody has legally rewritten.

---

## Working rules

- **XP wins every disagreement.** Record the conflict; do not "fix" XP.
- **`Source/` is read-only.** Never modify it to make a replacement work. Build side
  effects (regenerated `bugcodes`, rewritten `.lib`) are reverted, not committed.
- **`-Z`, never `-cZ`** for legacy builds — `-c` wipes `obj/` and destroys tracked
  prebuilt objects whose source is missing.
- **Measure, then claim.** Every flag in `SIZE-POLICY.md` was measured; two turned out to
  be folklore and are documented as such.
- **A check that cannot fail is not a check.** The runner's own negative control is the
  example.
- **Size is a budget**, not an afterthought: ≤64 KB `.text` per subsystem, `-Oz`.

---

## Next three actions

1. ~~**G1 — ABI proof.**~~ **Passed 2026-10-18** (`docs/G1-ABI-PROOF.md`).
2. ~~**G2 — conformance runner skeleton**, validated on the two kernels we already have.~~
   **Passed 2026-10-18** (`docs/G2-CONFORMANCE-RUNNER.md`): reports IDENTICAL on
   `xp.vmdk` vs `xp-pristine.vmdk`, and fails when it should.
3. ~~**P1 — first replacement**, to exercise the runner on something real.~~ **P1 complete
   2026-10-20, 6/6.** MD5, SHA-1, Base64, CRC-32 and NLS tables are gated on published
   vectors + negative controls (`anemos/{md5,sha1,base64,crc32,nls}/`); XP's base64 ABI is
   `crypt32!CryptBinaryToString/CryptStringToBinary` (advapi32 has neither on this build);
   XP exports no CRC-32, so that unit is vector-only. The `STATUS→DOS` map (item 6) is
   **verified against live XP for all 2^32 statuses** — six rules plus an 832-entry table,
   proved by an in-guest certificate with zero mismatches
   (`docs/P1-STATUSDOS.md`, `anemos/statusdos/run.sh` 14/14). Its raw-dump predecessor was
   structurally infeasible (>6 GB of triples and never reaching `0xC0000000+`); the
   model-verification probe that replaced it emits mismatches only and settled the map in
   three walks. Two documented mappings turned out to be wrong and XP won, as it must.
4. ~~**P2 — the DNS codec (RFC 1035)**: host-side parse/serialise with
   byte-identical round-trips and identical error codes on malformed input.~~
   **DNS codec complete 2026-10-20.** `anemos/dns/` — zero-copy RFC 1035 view,
   deterministic longest-suffix compression, and a 1,365-case differential corpus
   where an independent Python implementation and the C codec must agree on the
   status code, canonical bytes and reparse status (`docs/P2-DNS.md`, 12/12).
   Byte-identical round-trips are pinned by seven hand-written RFC vectors; the
   negative control proves the corpus gate can fail. XP's resolver behaviour is
   not claimed yet — it needs the socket path in P4.
5. ~~**P2 continued — the DHCP client (RFC 2131/2132)**: the same shape —
   host-side codec, independent implementation as the oracle, canonical
   round-trip, then the client state machine.~~ **DHCP codec complete
   2026-10-20.** `anemos/dhcp/` — RFC 2131/2132 header, options and builder;
   every accepted message round-trips byte for byte; a 4,095-case differential
   corpus against an independent Python implementation, 12/12
   (`docs/P2-DHCP.md`). Known gap: option overload (RFC 2132 §9.3) is not
   interpreted.
6. ~~**P2 continued — the DHCP client state machine**: DISCOVER/OFFER/REQUEST/
   ACK transitions, retransmission and lease timers, with the same
   differential oracle.~~ **DHCP client complete 2026-10-20.**
   `anemos/dhcp/dhcp_client.*` — deterministic, socket-free, with a scenario
   suite and a six-trace differential against an independent model; 24/24 with
   the codec (`docs/P2-DHCP-CLIENT.md`). Deliberate deviations: no jitter and a
   caller-seeded LCG for xids, both visible and documented.
7. ~~**P2 wrap-up — the DNS client** (query/response correlation, CNAME
   following, error mapping) using `anemos/dns/`, with the same shape: no
   socket, explicit time, a second implementation as the oracle.~~ **DNS
   client complete 2026-10-20.** `anemos/dns/dns_client.*` — case-insensitive
   matching, RCODE/TC mapping, bounded CNAME chains, and 2,040 differential
   cases; 24/24 with the codec (`docs/P2-DNS-CLIENT.md`).
   **P2 is complete.**
8. ~~**P3.1 — the CSP skeleton**: export all 23 required + 2 optional `CP*`
   entry points, with a host harness that `LoadLibrary`s the DLL and calls each
   one.~~ **CSP SPI skeleton complete 2026-10-20.** `anemos/csp/` — contexts,
   MD5/SHA-1/SHA-256, `CPGenRandom` via `SystemFunction036`, provider params,
   duplicate-hash; 11/11 (`docs/P3-CSP-SPI.md`). Key operations are explicit
   `NTE_NOT_SUPPORTED` stubs. XP still verifies provider signatures before
   `CryptAcquireContext`; the SPI is proved directly until that is decided.
   The import audit also caught that Wine is not XP: the first engine build
   imported ten Vista-only KERNEL32 APIs, so OpenSSL is now built without its
   threading layer and both crypto/CSP gates check imports against
   `xp-pristine.vmdk`.
9. ~~**P3.2 — symmetric keys**: `CPGenKey`, `CPDeriveKey`,
   `CPImportKey`/`CPExportKey`, `CPEncrypt`/`CPDecrypt` over OpenSSL AES, with
   known-answer and tamper tests through the SPI.~~ **P3.2a complete
   2026-10-20.** AES-128/192/256 key objects, CBC/ECB, PKCS#5/zero padding,
   `PLAINTEXTKEYBLOB`, derivation and one-shot cipher; SP 800-38A vectors and
   negative controls through the SPI (`docs/P3-CSP-SPI.md`). One-shot only:
   multi-call streaming returns `NTE_NOT_SUPPORTED`.
10. ~~**P3.2b — streaming cipher**: `CPEncrypt`/`CPDecrypt` across multiple
    calls with CryptoAPI's block-buffering semantics, then RSA (P3.3).~~
    **P3.2b complete 2026-10-20.** Per-key EVP stream state, multi-call
    block buffering, PKCS#5/zero padding, reset on parameter changes/final;
    multi-call vectors through the SPI (`docs/P3-CSP-SPI.md`).
11. ~~**P3.3 — RSA**: key blobs (`PUBLICKEYBLOB`/`PRIVATEKEYBLOB`),
    `CPSignHash`/`CPVerifySignature`, `CPGetUserKey`.~~ **P3.3 complete
    2026-10-20.** 1024-bit pairs, public/private blob export/import, ephemeral
    user keys, PKCS#1 v1.5 sign/verify, tamper and wrong-digest negatives.
    **P3.5 plumbing done under Wine** (`api_run.sh`, 7/7): CryptoAPI
    registration, parameter translation, AES/RSA through advapi32, and the
    little-endian signature convention proved against a second provider.
    Next: run the SPI harness inside XP (P3.4); the XP CryptoAPI path still
    waits on the provider-image signature decision.

Nothing else should be started before G1 and G2 pass. Everything after them is
substitution work behind stable interfaces, which is tractable precisely because the
interfaces do not move.

---

## Reference index

| Document | Purpose |
|---|---|
| `SCOPE.md`, `NOTICE.md`, `LICENSE` | What is ours; what is Microsoft's |
| `docs/OS-CODING-RULES.md` | 17 rules, each naming its failure mode |
| `docs/SIZE-POLICY.md` | Measured size levers and budgets |
| `docs/TOOLCHAIN-POLICY.md` | clang/lld 23.1.3, pinned in `tools/toolchain.lock` |
| `docs/TASK-REQUIREMENTS.md` | Tool and skill survey |
| `docs/X64-STATUS.md` | 21/23 subsystems build for AMD64 |
| `docs/KERNEL-BOOTS-XP.md` | The boot result and how to reproduce it |
| `docs/P1-STATUSDOS.md` | The STATUS→DOS map proved against XP for all 2^32 statuses |
| `docs/P2-DNS.md` | DNS codec: model, canonical compression, the differential gate, limits |
| `docs/P2-DNS-CLIENT.md` | DNS client: matching, CNAME chains, status semantics, the corpus gate |
| `docs/P2-DHCP.md` | DHCP codec: exact round-trip, option model, the differential gate, limits |
| `docs/P2-DHCP-CLIENT.md` | DHCP client: states, timers, determinism, the trace gate, limits |
| `docs/P3-CRYPTO.md` | Crypto engine evidence, CSP ABI, the signature constraint, the RAND hazard |
| `docs/P3-CSP-SPI.md` | CSP skeleton: implemented entry points, the LoadLibrary gate, limits |
| `docs/P4-NETWORK.md` | Network stack: slice plan, the open architecture decision, the address-layer gate |
| `docs/reference/P10-nasa-jpl.pdf` | NASA/JPL Power of Ten |
