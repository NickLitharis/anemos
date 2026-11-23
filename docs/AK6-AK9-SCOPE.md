# AK6–AK9 scope — kernel services, I/O, XP surface, network

Approved 2026-11-23. Scoped with the groundwork suite (planning-principles owns the
outcome/boundary format; modularity, unix-philosophy, composition, interface and
security-principles own the decisions noted per slice). `PLAN.md` remains the
authoritative status; this document is the scope for the remaining AK milestones.

## Goal

Take the independently authored kernel from AK6 ◐ (events, handles, timed waits
gated) to AK9: finished kernel services and security, an I/O/storage/driver path, a
prioritized XP-compatible syscall surface, and integrated networking — each slice
gated by `./boot/build.sh` with red-before-green tests and negative controls.

## What is already true (verified in-tree)

- Gate: `./boot/build.sh` — pinned clang 23.1.3 + `ld.lld` → Limine 12.9.6 BIOS ISO
  → QEMU q35 (15 s, serial to file), then grep for fixed `AKn: … ok` lines.
  Tests live in `kernel/*_test.c`, invoked from `main.c` self-test functions.
- AK6.1–AK6.3 in `kernel/event.c`: fixed pools (8 event objects / 16 handles),
  generation-checked handles, explicit rights, reference-counted close,
  manual-reset and timed waits (`event_wait`, `event_wait_for`), blocked waits pin
  the object. Serial line `AK6: event and handle tests ok`.
- Single-core only; SMP and exhaustive interrupt-interleaving proof are out of
  scope (AK4 recorded limit).
- P4.5 decision (`docs/P4-TRANSPORT-DECISION.md`): lwIP 2.2.0 (BSD-3) is the only
  acceptable network stack candidate; vendoring was blocked only on "no kernel
  network yet". AK9 unblocks it. XP NDIS/TDI (`anemos/net/kernel/`) stays
  reference evidence, never Anemos kernel code.

## Milestone slices

| Slice | Outcome (observable acceptance) | In / out |
|---|---|---|
| **AK6.4 wait cancellation** | `event_cancel_wait` wakes blocked waiters with `EVENT_STATUS_CANCELLED`; signaled state unchanged; signal-vs-cancel atomic with interrupts disabled | In: kernel-internal cancel. Out: SMP, user-visible cancel |
| **AK6.5 timer objects** | Bounded pool of one-shot/periodic kernel timers setting events or running a bounded callback; expiry ordered by tick | In: kernel timers. Out: DPC/APC generality |
| **AK6.6 per-process handle tables** | Handles namespaced per process (AK5 has 4 slots); stale-generation rejection across teardown; slot reuse restores baseline | In: namespace + rights at syscall entry. Out: full token/ACL model |
| **AK6.7 IPC** | Bounded message passing between processes: fixed-size messages, fixed-depth queues | In: port-like send/receive/wait. Out: shared memory |
| **AK6.8 user-visible security** | Fail-closed rights checks at the `int 0x80` boundary; probe+capture user buffers once (coding rule §3); every path tested allowed and denied | In: denial cases, invalid/stale handles, cross-process access. Out: audit, SIDs |
| **AK7.1 device/driver model** | Device object + bounded request dispatch (own request struct, no IRP transliteration); ISR→deferred-work contract, no blocking in interrupt path. First driver: QEMU e1000 NIC | In: q35 emulated devices. Out: DMA contract until needed, full PCI enumeration |
| **AK7.2 storage path** | Load user images from a device, not only in-memory ELF (AK5 limit). Limine modules first; AHCI + minimal read-only FS as the real path | In: read-only. Out: writable FS, journaling |
| **AK8 XP-compatible surface** | Prioritized documented/observable NT syscalls on `int 0x80` (object syscalls after AK6, file/device after AK7), NTSTATUS semantics per the P1-proved STATUS→DOS map | In: observable ABI. Out: undocumented internals, GUI |
| **AK9 network** | lwIP 2.2.0 (BSD-3, NOTICE) behind an Anemos-owned netif, fed by the AK7.1 e1000 driver; kernel sys-arch shim on fixed pools (rule §1). Gate: QEMU slirp UDP+TCP echo vs host peer, loopback | In: IPv4 UDP/TCP. Out: IPv6, NDIS/TDI |

## Sequencing

```
AK6.4 cancel ─┐
AK6.5 timers ─┼→ AK6.6 per-process handles → AK6.7 IPC → AK6.8 syscall-boundary security
              │                                              │
AK7.1 device model + e1000 ──────────────────────────────────┴→ AK8 object syscalls
AK7.2 storage (Limine modules → AHCI+RO FS) ───────────────────→ AK8 file/device syscalls
AK9 lwIP port starts once AK7.1 NIC exists; gated on AK6.8 (user buffers cross the boundary)
```

## Cross-cutting constraints (every slice)

- `docs/OS-CODING-RULES.md` rules 1–17: fixed pool-tagged pools, no hidden
  allocation (AK6's pool pattern is the template); no recursion/setjmp; bounded
  loops; ≤60-line functions; ≥2 side-effect-free asserts per function;
  `-Werror` with `-Wall -Wextra`; clang-analyzer on changed translation units.
- `docs/SIZE-POLICY.md`: `-Oz`, ≤64 KB `.text` per subsystem, ≤8 KB per TU,
  measured on the linked ELF (§S5).
- Test-first red→green with a negative control that must fail; new serial line
  grepped in `boot/build.sh`; document any test-first exception per rule §12.
- Provenance: original code or licensed third-party only; `Source/` read-only.

## Open decisions (decide before the slice starts)

1. AK6.5 timer callback model: event-set only (recommended first) vs bounded callback.
2. AK6.7 IPC shape: fixed-size messages with fixed-depth queues (recommended).
3. AK7.2: confirm Limine-modules-first vs straight to AHCI+FS.
4. AK8 syscall priority list and the differential harness (Anemos runs ELF probes,
   XP runs PE; the G2 runner drives XP VMs — "differential" = equivalent probe
   outputs on both, recorded as the contract).
5. AK9: lwIP pinned external fetch (like the Limine download in `build.sh`) vs subtree.

## Main risks

- AK6.8 is the real security boundary: malicious user process as attacker, kernel
  memory and other processes' objects as assets; enforce once, fail-closed, at the
  syscall entry; test every path (security-principles).
- Single-core QEMU gates cannot prove interrupt-interleaving correctness (AK4's
  recorded limit); negative controls are the evidence.
- Size: four milestones against a 64 KB/subsystem budget; measure the linked ELF at
  every gate.
