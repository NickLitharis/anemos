# Changelog

All notable changes to Anemos. The format loosely follows Keep a Changelog; the
releases here are the project's own annotated tags (`git tag`), so each phase can be
built, reviewed and diffed on its own. Dates are tag dates.

The README carries the overview and where the project stands. This file carries the
record of what changed.

## Unreleased

### Added

- **K3 — frame path.** NDIS packet-filter set, MAC query, a `ProtocolReceivePacket`
  handler that counts received frames and ARP replies, and an ARP request transmitted
  with `NdisSend`. `anemos/net/kernel/build.sh` produces a 68,608-byte `anemosnet.sys`
  whose `Ke*Event` imports resolve from `ntoskrnl.exe`. Guest verification of
  `traffic.tx` / `traffic.rx` is the remaining step.
- **K3 — NDIS protocol binding.** The NetTrans component installs through `INetCfg` and
  NDIS binds the protocol (`ndis.bound 1`, `bind_successes 1`) on the AMD PCnet NIC
  (`192.168.103.129`).
- **K2 — lwIP in the kernel.** `NO_SYS` build, kernel clock and internal netif; an
  in-kernel UDP round trip returns the same FNV-1a checksum as the user-mode
  implementation (`254371596`), and the driver still unloads.
- **K1 — loadable kernel driver.** `DriverEntry`, device and symbolic link, IOCTL ping
  returning `ANEM`, and clean unload in the guest.
- **P4 — network stack.** Winsock address layer with a 586-case corpus; transport-free
  socket lifecycle core; readiness and data path with a 30k-operation fuzz; resolver
  over the socket path; transport seam with a loopback reference. XP Winsock
  conformance is IDENTICAL 47/47. lwIP 2.2.0 (BSD-3) adopted, ReactOS rejected; the
  transport provider is blocked on the P5 scope decision.

### Changed

- Documentation corrected: the K3 frame path builds. `PLAN.md`, `README.md` and
  `docs/P5-NETWORK-KERNEL.md` had described a link failure that no longer existed.

## phase-p3.4 — 2026-10-21

### Added

- The CSP SPI harness runs inside XP through `LoadLibrary`: 163 checks, with PASS/FAIL
  lines identical to the host run. Certificates in `anemos/csp/xp/`.

## phase-p3.3 — 2026-10-21

### Added

- RSA keys and PKCS#1 v1.5 sign/verify: 1024-bit `CALG_RSA_SIGN`/`KEYX`,
  `PUBLICKEYBLOB`/`PRIVATEKEYBLOB` import and export with private round-trip, ephemeral
  `CPGetUserKey`, `CPSignHash`/`CPVerifySignature` with tamper and wrong-digest
  negatives. SPI gate 11/11.

## phase-p3.2b — 2026-10-20

### Added

- Streaming `CPEncrypt`/`CPDecrypt`: per-key EVP buffering, multi-call block semantics,
  PKCS#5 and zero padding, reset on final and parameter changes. Multi-call SP 800-38A
  vectors through the SPI.

## phase-p3.2a — 2026-10-20

### Added

- Symmetric key objects and one-shot AES: AES-128/192/256, CBC/ECB, PKCS#5 and zero
  padding, `PLAINTEXTKEYBLOB` import/export/duplicate, hash derivation, and SP 800-38A
  vectors through the SPI.

## phase-p3.1-xp — 2026-10-20

### Added

- XP hardening for the crypto engine: XP-safe OpenSSL build (no threads, async or
  thread pool) and the import-surface gate against `xp-pristine.vmdk`.

## phase-p3.1 — 2026-10-20

### Added

- CSP SPI skeleton: all 23 required plus 2 optional `CP*` exports, contexts,
  MD5/SHA-1/SHA-256, `CPGenRandom`, and provider parameters.

## phase-p3.0 — 2026-10-20

### Added

- OpenSSL 3.5.7 cross-built for i686 Windows; SHA/HMAC/AES/RSA known-answer tests pass.

## phase-p2 — 2026-10-20

### Added

- DNS codec and client, DHCP codec and client, each with an independent second
  implementation as the oracle and a differential corpus of valid, truncated and
  mutated inputs.

## phase-p1 — 2026-10-20

### Added

- MD5, SHA-1, Base64, CRC-32 and NLS tables, gated on published vectors with negative
  controls.
- `STATUS→DOS` map proved against live XP for all 2^32 statuses by an in-guest
  certificate with zero mismatches (`anemos/statusdos/run.sh`, 14/14).

## phase-p0 — 2026-10-18

### Added

- **G1 — ABI proof** (15/15): clang 23.1.3 and the tree's MSVC 13.00 agree on struct
  layout, calling convention and name mangling, i386, with a negative control.
- **G2 — conformance runner**: boots a VM, runs tests inside XP, collects observations
  and diffs subject against control. Reports IDENTICAL for `xp.vmdk` against
  `xp-pristine.vmdk`, and both failure directions were demonstrated.

## Before the phase index — 2026-10-18

### Added

- Freestanding boot experiments: a bootable image under QEMU (v0.1), real XP SP1 source
  executing under QEMU (v0.2), and a growing set of kernel source files verified the
  same way.
- The razzle build harness: an XP source reader with legacy-versus-modern repairs,
  documented with its coverage limits.
- An in-guest `DbgPrint` tracing shim and the `STATE.md` inventory of what was verified.
- The first `STATUS→DOS` error table (835 pairs), generated and verified — the
  predecessor of the exhaustive P1 proof.
