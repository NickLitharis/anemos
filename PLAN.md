# Anemos — the plan

**This document supersedes `MODERNIZATION_PLAN.md`, `docs/OPEN-REPLACEMENT-PLAN.md`,
`docs/ANEMOS-PLAN.md` and `STATE.md`.** Those are kept for history; where they disagree
with this, this wins. They used three incompatible numbering schemes (M0-M4, R0-R5, P0-P5)
for the same work, which is a reliable way to lose track of what is done. `STATE.md` still
says "M0 is done and M2 is the live track": that numbering is dead, and its kernel-boot
result is now row 1 of the table below.

---

## The goal, stated once

Build Anemos as an **original, open-source kernel and operating system inspired by Windows XP**, with compatibility judged against XP's observable behaviour. XP is the reference system, never Anemos's codebase.

Anemos must boot and run its own kernel without Microsoft's kernel. Compatibility is an outcome to earn subsystem by subsystem, not a claim that the kernel can reproduce undocumented internals exactly.

Four constraints follow, and they are not negotiable:

1. **Independent implementation.** No Microsoft source or mechanically transliterated logic in Anemos. Use public specifications and observed behavior; keep provenance for new code.
2. **Open-source boundary.** Only files with established original or third-party licensing may ship as Anemos. Existing XP-derived experiments require provenance and release review.
3. **Compatibility.** XP is the behavioral oracle. Record differences; do not change XP to make a test pass.
4. **Standards.** C23 / C++23, clang 23.1.3, `docs/OS-CODING-RULES.md` and `docs/SIZE-POLICY.md`. Apply the documented NASA/JPL Power of Ten adaptations and test-first workflow below; do not claim strict NASA compliance or certification.

---

## Where we actually are

Status is backed by explicit gates, not compilation alone.

Host-side gates last re-measured 2026-11-01: all 17 exit 0 with 162 passing assertions
and no skips; the 8 XP guest gates were not run. The expanded AK4 and AK5
privilege-boundary QEMU gates passed on 2026-11-14 with `./boot/build.sh`. The
completed AK5 process/address-space/ELF gate also passes: its serial log includes
the AK3, AK4, all three AK5 markers, and AK2 timer/fault checks. A ✅ records only its
named gate, not production readiness, exhaustive verification, or coding-standard compliance.

Legend: ✅ gated and passing · ◐ partial, with the missing piece named · ⏳ not started or not run.

| Capability | Status | Evidence |
|---|---|---|
| Build XP's kernel from source | ✅ | 59 libraries, 1,464 exports matching Microsoft's |
| Boot it into a full XP install | ✅ | `docs/KERNEL-BOOTS-XP.md` |
| Build 21/23 kernel subsystems for **AMD64** | ✅ | `docs/X64-STATUS.md` |
| Modern compiler → legacy libs → working PE | ✅ | clang 23.1.3 + `lld-link` on real `ntosrtl.lib` |
| Two bootable kernels, one disk (the oracle) | ✅ | `xp-pristine.vmdk` / `xp.vmdk` |
| Host-side binary swap, no root | ✅ | `tools/ntfspatch.py` |
| Legal/source provenance boundary | ◐ | AK0 removed Microsoft-derived `kernel/` and `boot/` experiments from checkout/history; AK1 files are independently authored. The ignored external `Source/` reference remains outside the repository. |
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
| **P4.1 — Winsock address layer** | ✅ | Byte order, `sockaddr_in`, IPv4 `inet_pton`/`inet_ntop`/`inet_addr`/`inet_ntoa`; 586-case corpus against Python `ipaddress`, negative control rejects a wrong renderer; `addr.c` 843 B `.text` (measured by `anemos/winsock/run.sh`). The P4.6 certificate is what added the legacy parser: XP's `inet_addr` accepts 1-4 parts, octal and hex, and returns 0 for a leading space. `inet_pton` stays strict because XP has no such API (`docs/P4-NETWORK.md`). |
| **P3.5 — signature experiment** | ✅ | Every documented `CryptAcquireContext` route returns `NTE_BAD_SIGNATURE` on stock XP; the stock provider still acquires. Decision: no `advapi32` patch; XP CryptoAPI recorded as unclaimable (`docs/P3-CRYPTO.md`, `anemos/csp/xp/csp-sig.txt`). |
| **P3.6 — RAND hardening** | ✅ host | OpenSSL patched to seed from `SystemFunction036` (not CryptoAPI), built `no-engine`, and explicitly seeded at first context; the gate proves the DLL imports no `Crypt*` entry points; guest recursion test waits on the P3.5 route (`docs/P3-CRYPTO.md`). |
| **P3.4 — SPI harness inside XP** | ✅ | 163 checks through `LoadLibrary` in the guest; PASS/FAIL lines identical to the host run, certificates in `anemos/csp/xp/` (`docs/P3-CSP-SPI.md`). Guest PEs must declare subsystem 5.01. |
| **P4.6 — XP Winsock observation probe** | ✅ | Captured in the guest: `anemos/winsock/xp/winsock-cert.txt`, 47 observations plus the completion banner. The host twin reproduces it identically, and the comparator rejects a changed, missing or extra case; probe gate 14/14 (`docs/P4-NETWORK.md`). This closes the "XP quirks deferred" note on every P4 slice below. |
| **P4.5 — lwIP spike (MIT route)** | ✅ start | lwIP 2.2.0 (BSD-3) pinned outside the tree, built `NO_SYS` for i686 Windows; loopback UDP round-trip gated 4/4 with a negative control. Adapter to the socket-core seam is next (`docs/P4-TRANSPORT-DECISION.md`). ReactOS rejected. |
| **P4.5 — transport seam + loopback** | ✅ reference | `open/bind/listen/connect/accept/send/shutdown/close/refuse` requests with injected events; 27 end-to-end assertions over TCP, UDP, EOF, refusal and backlog, negative byte-drop control; `loopback.c` 1061 B `.text` (`docs/P4-NETWORK.md`). The real TDI/WSK or adopted-stack provider remains the open decision. |
| **P4.4 — resolver over the socket path** | ✅ | DNS A/AAAA over the socket core: explicit-time retries, in-message CNAME chains, fresh xid per restart, 18 differential traces (ok/dns/timeout all exercised), negative control on every ok trace; `resolver.c` 922 B `.text` (measured by `anemos/dns/resolver_run.sh`). Closes P2's deferred socket path. |
| **P4.3 — readiness and data path** | ✅ | `select` over read/write sets, non-blocking `send`/`recv`, EOF/reset, `WSAEWOULDBLOCK`/`WSAESHUTDOWN`/`WSAECONNRESET`; 40 traces match the model, 30k-op fuzz clean, `socket_core.c` 2296 B `.text` (measured by `anemos/winsock/socket_run.sh`). No blocking wait — the transport owns that. |
| **P4.2 — socket lifecycle core** | ✅ | Transport-free state machine for `socket`/`bind`/`listen`/`connect`/`accept`/`shutdown`/`close`, with a drained request queue and injected events; 26 traces match an independent Python model, a broken accept queue is rejected, `socket_core.c` 1261 B `.text` as built at P4.2, 2296 B after P4.3 (`docs/P4-NETWORK.md`). |
| **P3.5 — CryptoAPI plumbing** | ✅ Wine | Registered in a Wine prefix and driven through advapi32: acquire, hashes, random, AES-CBC vector, RSA sign/verify, plus a cross-provider check proving the little-endian CryptoAPI signature convention; 7/7 (`docs/P3-CSP-SPI.md`). Stock XP's image-signature check still blocks the real XP path. |

P0–P4 delivered a valuable host-side conformance method and reusable, independently implemented libraries and protocols. They are not a kernel.

The historical `kernel/` and `boot/` XP-source experiments were removed. AK1–AK5
now contain the independently authored kernel: Limine boot, CPU exceptions and
timer interrupts, physical page allocation, 4-level virtual mapping, a bounded
64-KiB heap, single-core kernel threads, a CPL3 syscall/fault probe with bounded
buffer copy-in, four isolated process address spaces, and a validated static
in-memory ELF64 loader with teardown. General I/O services, filesystem loading,
and scheduling remain deferred. `anemos/net/kernel/` remains an XP-loaded driver
experiment, not an Anemos kernel.

The project now targets an independently implemented, bootable XP-inspired kernel. P0–P4 remain reusable compatibility work; they no longer define project completion.

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
| **P3** | Crypto: engine (**done, XP-safe**), SPI skeleton (**done**), AES keys/streaming (**done**), RSA sign/verify (**done**), the XP guest run (**done**), then the CryptoAPI path | known-answer tests through the SPI; CryptoAPI integration is constrained by XP's provider-signature check — see `docs/P3-CRYPTO.md` |
| **P4** | Winsock/network compatibility libraries and model gates; reusable user-mode and protocol work, not kernel completion. Transport choice is still open (`docs/P4-TRANSPORT-DECISION.md`). |
| **AK0 — provenance and architecture** | ✅ | Removed XP-derived `kernel/` and `boot/` experiments from checkout, local refs/tags, and GitHub. First target: C23, x86_64, Limine, QEMU, serial debug output. |
| **AK1 — independent boot** | ✅ | `./boot/build.sh` builds the freestanding ELF, makes a Limine BIOS ISO, boots QEMU, and verifies `ANEMOS: boot ok` on serial. No XP kernel or copied XP RTL in the boot path. |
| **AK2 — CPU and platform foundation** | ✅ | `./boot/build.sh` verified IDT exception vectors 0–31, remapped PIC/PIT IRQ0 ticks, and an injected divide-by-zero panic on QEMU serial after three timer ticks. |
| **AK3 — memory management** | ✅ | `./boot/build.sh` verifies Limine memory-map/HHDM setup, zeroed 4-KiB allocation/free/reuse, page mapping and aliases, 64-KiB heap alignment/split/coalesce/OOM/reuse, then AK2 timer/fault checks on QEMU. |
| **AK4 — execution and synchronization** | ✅ single-core gate and targeted negative controls; exhaustive validation pending | `./boot/build.sh` exercises cooperative switching, timer preemption, semaphore signal-before-wait and blocked-waiter wakeup, timed waits, idle wakeup, thread-slot exhaustion and reuse after termination. Temporary fault injections losing an early semaphore signal, preventing thread reclamation, and disabling interrupts in the idle halt each made the QEMU gate fail with the corresponding AK4 test failure or timeout; restored-source gate passes. SMP and exhaustive interrupt-interleaving verification remain out of scope. |
| **AK5 — processes and system-call boundary** | ✅ bounded process, ELF and user-buffer syscall slices; general I/O remains | `./boot/build.sh` verifies four isolated process roots, bounded static ELF64 loading, CPL3 execution, page permissions, rejection/cleanup cases, process teardown/slot reuse, a bounded one-page copy-in checksum syscall with invalid-range rejection, and retained AK2–AK5 gates. |
| **AK6 — kernel services and security** | ◐ kernel-internal events, handles, timed waits and wait cancellation gated | `./boot/build.sh` gates fixed-capacity events, generation-checked handles, rights, reference-counted close, exhaustion/reuse, manual-reset and relative-timeout waits, and wait cancellation (AK6.4, step 8). Timer objects, IPC, per-process handle tables and user-visible security remain unimplemented. |
| **AK7 — I/O, storage and drivers** | ⏳ Device/driver model, request dispatch, interrupt/DMA contracts, and a usable filesystem/storage path; start with emulated devices and bounded integration tests. |
| **AK8 — XP compatibility surface** | ⏳ Implement prioritized documented/observable NT-compatible system calls, exports and driver ABI; differential tests against XP. Do not promise undocumented internal identity. |
| **AK9 — network integration** | ⏳ Integrate independently licensed networking behind Anemos-owned kernel interfaces; XP NDIS/TDI experiments remain reference evidence, not the Anemos kernel. |

AK0–AK5 have passing QEMU gates for the implemented slices. AK5 now includes
bounded process address spaces, executable loading and a user-buffer syscall
with checked one-page copy-in. SMP and general I/O remain out of scope. Do not
extend the XP-only network driver as a substitute for kernel work.

P1 comes before P2 because it needs no VM, so the runner gets exercised cheaply before it
is asked to judge anything hard.

---

## Open decisions

The first target is decided: x86_64, Limine boot, QEMU, C23, and serial debug output. The P4 transport provider remains open for the XP network-replacement track and does not block AK0/AK1.

XP's stock CryptoAPI image-signature behavior is a documented constraint, not a kernel prerequisite.

### Verification boundary

XP can validate observable ABI and behavior, but it cannot be the specification for every internal kernel detail. Each subsystem needs an explicit contract, independent tests for internal invariants, and differential XP tests only where behavior is observable.

### The one dependency

Every XP guest gate needs this host: VMware, the `vmmon`/`vmnet` modules, and
`~/vms/xpvmware/{xp-pristine,xp}.vmdk`. The independent AK kernel gate uses QEMU
and Limine instead; it does not require those XP disks. The XP environment is a
single point of failure for compatibility testing and has already stopped work
once — `docs/P4-NETWORK.md` recorded the root
filesystem as read-only and VMware as unable to start, which is why the guest gates
stopped. On 2026-11-01 `/` is mounted `rw`, both modules are loaded and `vmrun list`
answers, so that note is stale; the exposure it describes is not.

## Scope boundaries

**Included:** an independently bootable kernel; boot/CPU/platform support; interrupt handling; memory, scheduling, process/thread and syscall services; object/handle, IPC and security services; I/O, storage/filesystems and drivers; and a defined XP-inspired ABI. Network compatibility is one subsystem, not the whole kernel.

**Not automatically included:** a bit-for-bit clone of XP internals, undocumented behavior that cannot be observed, a Windows desktop, or wholesale reuse of Microsoft code. GUI and application compatibility require separate milestones after a bootable kernel exists.

**Source provenance is a release gate.** The tracked XP-source experiments
formerly under `boot/` and `kernel/` have been removed; those paths are reserved
for independent implementation. See `SCOPE.md` for the current boundary.

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

### Development workflow

`docs/OS-CODING-RULES.md` is the detailed authority. Its NASA/JPL-inspired
profile requires simple control flow, bounded loops, small functions, checked
results, side-effect-free assertions, narrow scope and static analysis. Kernel
exceptions are documented there; compliance must be checked, not inferred from
a successful build. TDD complements these rules; it is not a NASA certification.

For each new behavior or bug fix:

1. **Contract:** specify inputs, ownership, errors, resource bounds and invariants.
   For concurrent code, include interrupt context, protected state and wait/wakeup
   transitions. Acceptance: each requirement has an observable check.
2. **Red:** write a focused behavioral test and observe its intended failure before
   implementing. Use deterministic host tests where possible and QEMU/hardware for
   CPU-dependent behavior. Document an impractical test-first case and its runnable
   substitute, as permitted by coding rule 12.
3. **Green:** implement the smallest correct change and run the focused check.
   Acceptance: the test passes without weakening its contract.
4. **Refactor and verify:** simplify, review against coding rules, run applicable
   compiler/static-analysis checks and the affected gate, and measure linked code
   size against policy. Validate critical tests with negative controls. Record
   commands, results and unverified limits; update status and provenance documents.

The expanded AK4 checks were added after the implementation already passed, so no
red-before-green result exists for those changes. Three temporary fault-injection
negative controls did demonstrate that the tests reject a lost early semaphore
signal, unreclaimed terminated threads and an idle halt with interrupts disabled.
The test orchestration was split into small helpers to meet §14. The gate is still
single-core and does not exhaustively explore interrupt timing; existing atomic
critical sections were reviewed, not model-checked.

---

## Next actions

1. ~~**Close AK4 validation before AK5.**~~ **Completed 2026-11-14.** Reviewed
   scheduler code and contracts, split the oversized test orchestration, ran
   temporary fault-injection builds for semaphore signal retention, thread reuse
   and idle wakeup (each failed in the intended AK4 path), then reran the restored
   `./boot/build.sh` successfully. No red-before-green history for the tests; no
   exhaustive interrupt-interleaving or SMP proof. Evidence is the serial log at
   `out/ak3/serial.log`; the successful gate prints `AK4: thread tests ok`.
2. ~~**AK5 contract and privilege separation.**~~ **Completed 2026-11-14 (static-probe boundary only).** Entered one static probe in a
   dedicated CR3 derived from Limine's root: copy the present high-half PML4
   entries while clearing U/S at the PML4 level; leave lower-half entries empty.
   The supervisor PML4 bit prevents CPL3 access to every inherited mapping below
   that entry, regardless of lower-level bits. This proves CPL3/kernel permission
   enforcement, not process lifecycle or multi-process isolation. Reserve user
   virtual addresses `[0x00400000, 0x0000800000000000)`; keep page zero and lower
   addresses unmapped. Allocate dedicated user code and stack pages; never map
   kernel text as user code. Build fresh lower-level user tables with U/S set at
   each level, and reject large-page collisions. Code is read/execute; stack is
   read/write/NX. Kernel image, IDT/GDT/TSS, entry stacks, HHDM and MMIO remain
   supervisor-only in the test CR3. NX is mandatory: fail setup if NX/NXE is
   unavailable. Verify the new root's effective permissions before CPL3.

   Install and load an Anemos-owned GDT with named ring-0/ring-3 code and data
   selectors plus a 64-bit TSS; reload CS/data selectors and execute `ltr`.
   Configure IDT vectors 0–31 with the ring-0 code selector and add vector `0x80`
   as a DPL3 interrupt gate. TSS RSP0 points to a dedicated, supervisor-only
   kernel-entry stack. Use `iretq` for first CPL3 entry; sanitize user RFLAGS
   (IOPL, NT and VM clear, reserved bit set). Keep user IF clear for this probe;
   software syscall entry still works, while asynchronous CPL3 scheduling is
   deferred until its entry path is designed.

   Initial `int 0x80` ABI is scalar-only: RAX=1 adds 7 to RDI and returns the
   result in RAX; RAX=2 exits the probe; unsupported service numbers return
   `UINT64_MAX`. No syscall dereferences user pointers. On CPL3 entry, inspect
   saved SS/RSP only after confirming saved CS has RPL 3. The syscall result
   returns to user with `iretq`.
   A CPL3 page fault or general protection fault records vector/error/RIP and
   returns to an explicit ring-0 recovery trampoline on the saved kernel-entry
   stack; it must not retry the faulting user instruction or expose a kernel
   address. CPL0 exceptions retain the panic path.

   The first probe deliberately reads a supervisor mapping and executes a
   privileged instruction to test containment. The probe itself does not provide
   a general executable loader, process lifecycle, multi-process isolation, or
   user-buffer ABI.

   **Acceptance:** a user-mode probe calls the scalar service and returns the
   expected result; supervisor-page read (#PF error 5), code write (#PF error 7),
   stack execution (#PF error 21) and privileged `cli` (#GP error 0) are contained.
   The kernel continues, and the AK4 and AK2 gates still pass. The restored-source
   `./boot/build.sh` log includes `AK5: privilege tests ok`.

   Temporary negative controls confirmed the test detects regressions: a wrong
   syscall result failed the scalar test; omitting #PF and #GP handling produced
   the expected kernel panic paths (vectors 14/error 5 and 13/error 0). All
   mutations were restored before the passing acceptance run.

   **Test-first exception:** no CPL3 transition harness existed before this first
   vertical slice. Its transition, handlers and initial behavioral checks were
   implemented together, so there was no red-before-green run for this slice.
   The QEMU acceptance gate and later negative controls validate the behavior and
   failure detection. New AK5 behavior must use the normal test-first workflow.
3. ~~**AK5 processes and executable loading.**~~ **Completed.** Four user roots and four
   process slots, with bounded in-memory static ELF64 loading and teardown, are
   gated below.
   a. Support four simultaneously owned user CR3 roots. Each 32-page ledger
   includes its root, every lower-level page-table page, image page and stack
   page; inherited kernel mappings are shared and never freed by process teardown.
   Reject destruction while a root is active. Red/green QEMU test: two roots map
   one user VA to different pages, read different values after CR3 switches, then
   return `page_free_count()` to baseline after destruction.
   b. Add a fixed four-slot process table and an in-memory loader for static,
   little-endian x86-64 ELF64 `ET_EXEC`. Accept only 1–8 `PT_LOAD` headers;
   reject all other header types. Require ELF/current version, exact header sizes,
   `EM_X86_64`, page-aligned file/virtual starts, `p_align == 4096`, `p_filesz <=
   p_memsz`, checked file/virtual ranges, non-overlapping pages, `PF_R`, no
   unknown flags or W+X, and entry inside file-backed executable bytes. Limit to
   eight image pages in `[0x00400000, 0x40000000)` plus one fixed RW/NX stack
   page. Zero allocated pages before copying bytes so BSS is zero. Reject dynamic
   or interpreted images. The ledger limit includes all page-table overhead;
   any allocation/map failure must tear down all partial state and release the slot.
   c. Acceptance: create four live processes; reject a fifth without changing the
   free-page count; reject destroying an active root; destroy one, then reuse its
   slot. Two valid images put different data at the same VA and execute through
   CPL3, each returning its own value. Verify code RX, data RW/NX, stack RW/NX,
   read-only segment enforcement, malformed image rejection and exact free-page
   baseline after failure/destruction. `./boot/build.sh` retains AK2–AK5 gates.
   This slice loads in-memory images only; scheduling, dynamic linking, filesystem
   lookup and user-buffer syscalls remain deferred.

   **Acceptance evidence:** the restored-source `./boot/build.sh` gate prints
   `AK5: address-space tests ok` and `AK5: process tests ok`, then continues
   through `AK2: timer ok` and the expected injected divide-by-zero panic.
   Tests prove same-VA isolation and CPL3 execution of two distinct images,
   RX/RW-NX/stack permissions, malformed-image rejection, four-process capacity,
   active-root destroy refusal, slot reuse and exact page-count restoration.
   Test-first red runs rejected a second root and rejected a valid ELF before
   implementation. Negative controls for `p_filesz > p_memsz` and rollback after
   an allocated-but-unmapped image page failed the gate; the restored gate passed.
   Strict pinned-clang diagnostics passed on every changed C translation unit.
   This does not validate scheduling, dynamic linking, filesystem loading or
   general user-buffer syscall safety.
4. ~~**AK5 system-call and buffer-transfer contract.**~~ **Completed.** `int 0x80`
   service 3 takes the user buffer address in RDI and length in RSI, accepts
   1–32 bytes contained in one mapped user page, and returns their unsigned-byte
   sum in RAX. Invalid, overflowing, unmapped, supervisor-only and cross-page
   ranges return `UINT64_MAX` without privileged side effects. CPL3 tests cover a
   valid transfer plus null, unmapped, cross-page, overflowing, zero-length and
   oversized requests. The restored `./boot/build.sh` gate passes all AK2–AK5
   checks. Filesystem and general I/O services remain outside this contract.
5. **AK6.1 — kernel event objects and handles.** Fixed pools of eight event objects
   and sixteen handles; opaque generation-checked handles; explicit signal/reset/query
   rights; duplicate handles can only reduce rights; close drops the reference and
   frees the object after its last handle closes. **Completed:** `./boot/build.sh`
   prints `AK6: event and handle tests ok` and continues through AK2. Tests cover
   event state transitions, denied rights, stale handles, duplicate lifetime,
   object/handle exhaustion and slot reuse. A temporary broken-set mutation failed
   at the state-transition test; restored-source gate passed. Strict pinned-clang
   diagnostics and clang-analyzer passed. Blocking waits, timers, IPC, per-process
   handle tables, and user-visible security need separate contracts.
   **TDD exception:** AK6.1 implementation preceded its tests. The later
   negative control demonstrates fault detection, not red-before-green TDD.

6. **AK6.2 — manual-reset event waits.** On a single CPU, a wait requires
   query access and returns immediately for a signaled event; otherwise atomically
   register and block until set. Set wakes all registered waiters and remains
   signaled until reset. A blocked wait pins its object even if all handles close;
   signaling requires another live handle. Wait registration, set, reset, close,
   wakeup and object reclamation are serialized with interrupts disabled.
   TDD evidence: a temporary nonblocking stub failed the immediate-wait test;
   the implementation then passed `./boot/build.sh`. Tests exercise an already
   signaled event, reset, denied access, two blocked waiters, close of their
   handle before another handle signals, and both waiters waking. Timed waits,
   cancellation and SMP remain outside this contract.

7. **AK6.3 — timed event waits.** Add a kernel-only relative tick timeout:
   signaled events succeed immediately; zero ticks polls; otherwise block and
   report timeout if the timer wins. Signal and timeout resolve atomically with
   interrupts disabled, and the winning result persists even if the event is
   reset before the waiter resumes. Pin the event throughout a blocked wait.
   **TDD evidence:** the QEMU gate first failed with `AK6: timed event polling
   failed` against the unsupported stub; the implemented path then passed.
   Tests exercise both polling outcomes, timeout after three ticks, access denial,
   and signal-before-expiry after closing the waiter's handle. Strict clang
   diagnostics and clang-analyzer passed. Cancellation, SMP, exhaustive
   signal/timeout interleavings and XP compatibility remain unverified.

8. **AK6.4 — wait cancellation.** `event_cancel_wait(handle)` cancels all blocked
   waits on the event object (query access required); waiters wake with
   `EVENT_STATUS_CANCELLED`, the signaled state is unchanged, and signal-vs-cancel
   resolves atomically with interrupts disabled. Scope and sequencing for
   AK6–AK9: `docs/AK6-AK9-SCOPE.md`. Gate: `./boot/build.sh` prints
   `AK6: wait cancellation tests ok`; a no-op stub fails the gate first (red).
   Negative controls: no-waiter cancel, denied access, stale handle,
   close-while-blocked. **Completed 2026-11-23.** Red run: a no-op stub failed the
   gate with `AK6: wait cancellation failed`; the implemented path then passed and
   `./boot/build.sh` prints `AK6: wait cancellation tests ok` (see
   `out/ak3/serial.log`). `event_wait`/`event_wait_for` now return
   `EVENT_STATUS_CANCELLED` when cancelled; `thread_cancel_channel_locked` wakes
   waiters with the cancelled flag set, atomically with interrupts disabled, and
   the event's signaled state is unchanged (a later set still wakes a new waiter).
   clang --analyze clean on the changed TUs; linked `.text` 18,181 B. One
   test-authoring fix during green: the close-while-blocked phase must close the
   second handle before asserting object reclamation. SMP, exhaustive
   signal/cancel/timeout interleavings and XP compatibility remain unverified.

### Completed, in order

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
    The SPI harness then ran inside XP (P3.4, 163 checks); the XP CryptoAPI path still
    waits on the provider-image signature decision, open decision 3 above.

That list is history, not a queue. G1 and G2 have both passed, and everything after them
was substitution work behind stable interfaces — tractable precisely because the
interfaces did not move.

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
| `docs/P4-NETWORK.md` | Network stack: slice plan, the address-layer gate, the P4.6 probe |
| `docs/P4-TRANSPORT-DECISION.md` | P4.5 memo: lwIP vs ReactOS vs stop; the kernel boundary |
| `docs/P5-NETWORK-KERNEL.md` | K1-K5: driver loading, NDIS binding, the frame path, the remaining guest run |
| `docs/G1-ABI-PROOF.md`, `docs/G2-CONFORMANCE-RUNNER.md` | The two gates, with their negative controls |
| `CHANGELOG.md` | What changed, by release |
| `STATE.md` | Historical: the freestanding era and the kernel-boot result, in dead M-numbering |
| `docs/reference/P10-nasa-jpl.pdf` | NASA/JPL Power of Ten |
