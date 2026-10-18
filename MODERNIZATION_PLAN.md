# XP SP1 Modernization — Master Plan

Tree: `/home/marocos/winxp_source/Source/XPSP1/NT` — 67.8M LOC, 277,865 files, commit `a2f6d7c93`.
Goals, in the user's words: **x64 · faster · no Microsoft-proprietary code · identical looks.**

This plan is ordered by *dependency*, not by ambition. Each milestone has a
**gate**: a check that can fail. A milestone is not done until its gate passes.

> **Status (2026-10-18):** the kernel is no longer hypothetical. All 59 kernel
> libraries build with the tree's own razzle toolchain under Wine, and the resulting
> `ntoskrnl.exe` **boots a full XP SP1 installation to a working desktop**.
> See `docs/KERNEL-BOOTS-XP.md` and `docs/KERNEL-BUILDS.md`. M0 and the first half
> of M2 are therefore already done; this document has been re-based on that fact.

---

## Part 1 — What we already have

### 1.1 The source tree (verified)

| Fact | Value |
|---|---|
| Tracked files | 277,865 (all present on disk, `git status` clean) |
| LOC by language | C 24.0M · C++ 21.0M · headers 12.6M · `.cxx` 7.3M · asm 0.93M · rc 0.75M · idl 0.72M |
| Kernel | `base/ntos` — 1,715 files (ke 283, rtl 204, config 162, io 129, mm 105, ex 85, init 79, se 68) |
| HAL | `base/hals` — halx86, halmps, halacpi, halsp, **halia64**, x86new. **No halx64.** |
| win32k | `windows/core` — 4,168 files (ntuser, ntgdi, ntcon, kmode, dxkernel) |
| Shell | `shell` — 14,763 files (explorer, shell32, themes) |
| User DLLs | `base/win32/client` (kernel32), `base/win32/server` (user32), `windows/core/ntgdi/client` (gdi32) |
| ntdll | `base/ntdll` — includes `amd64/`, `i386/`, `ia64/`, `wow64/` |
| x64 code present | **71** `.c/.h/.asm` under `base/ntos` amd64 dirs (vs 141 i386), plus 30 files in `base/hals/halacpi/amd64`. **21 of 23 kernel subsystems build for x64 today** — see `docs/X64-STATUS.md`. |

### 1.2 Chokepoint headers (how to read the tree cheaply)

- `base/ntos/inc/ntos.h` (95 lines) → pulls **83** kernel headers
- `base/ntos/ke/ki.h` (1,153 lines) → `ntos.h` + `amd64\kiamd64.h` + `i386\kix86.h`
- `base/ntos/mm/mi.h` (7,483 lines) → `mm\i386\mi386.h` | `mm\amd64\miamd.h` | `mm\ia64\miia64.h`
- `base/win32/client/basedll.h` (1,171 lines) → kernel32 private surface
- win32k: monolithic `precomp.h`

### 1.3 MSVC-only constructs (the M0 blockers, counted by file)

`__declspec` 2,099 · `__try`/SEH 2,112 · `__except` 1,520 · `__stdcall` 1,707 · `_asm` 602 · `__fastcall` 209

### 1.4 Build system reality

Razzle, not a bootstrap: `makefil0` → `!INCLUDE $(NTMAKEENV)\makefile.plt`, `sources`/`dirs`,
`build.exe`/`nmake`/`binplace`, `_NTBUILD`, MSVC 6 + DDK. `tools/bcbuild.js` is a WSH/ActiveX
build-manager client, **not** a compiler script.

### 1.5 Tooling installed

| Tool | Path | Status |
|---|---|---|
| Universal Ctags 6.2.0 | `~/.local/bin/ctags` | ✅ **5,333,276 tags** over 115,078 files |
| cppcheck | `~/.local/bin/cppcheck` | ✅ |
| wine + `~/.wine-xp` prefix | system | ✅ runs the tree's **own** MSVC toolchain |
| **In-tree MSVC toolchain** | `NT/tools/x86/` | ✅ `cl.exe c1.dll c1xx.dll link.exe nmake.exe ml.exe rc.exe build.exe` |
| mingw-w64 | `~/.local/opt/llvm-mingw-20261006-msvcrt` | ✅ for user-mode probes |
| REA + Ghidra 12.1.4 | `~/ghidra/`, MCP in `~/.pi/agent/mcp.json` | ✅ 209 tools |
| `harness/razzle.sh` | `harness/` | ✅ drives razzle under Wine |
| `legacy-nt-modernization` skill | `~/.pi/agent/skills/` | ✅ loaded (census baked in) |

### 1.5a Build capability (measured, not assumed)

| Claim | Evidence |
|---|---|
| 59 kernel libraries build | every `base/ntos/*` arch variant: up, mp, pae, paemp |
| `ntoskrnl.exe` links | 1,918,976 bytes, **1,464 exports — same name set as Microsoft's**; `.edata` size matches exactly (0xb258) |
| Real debug info | 6.1 MB `ntoskrnl.pdb` |
| Supporting binaries | `hal.dll` 101,376 · `bootvid.dll` 12,288 · `kdcom.dll` 7,168 |
| **It boots** | full XP SP1 install reaches Luna desktop on our kernel; verified by hashing the kernel back out of the VMDK |

Two non-`base/ntos` libraries are required for the link: `base/boot/bootvid` and
`base/boot/kdcom`, built before `base/ntos/init/up`.

**Rule:** always build with `-Z`, never `-cZ`. `-c` wipes `obj/`, which destroys tracked
prebuilt `.obj` files whose sources are missing from the leak — sometimes the only copy.

### 1.6 Known dead ends (do not retry)

- 8 LFS pointers (`enduser/speech/tts/truetalk/voices/*/*.sfont`) — 404 server-side, permanent.
- A 34B-class local model — 15.5 GiB RAM, no GPU; needs ≥40 GiB.
- **Replacing a running kernel from inside Windows** — the file is locked. Explorer's
  replace dialog accepts "Yes" and silently does nothing; `copy /y` fails. Use the
  host-side VMDK patcher (`tools/ntfspatch.py`) with the VM powered off.
- **Typing `:` over VMware's VNC server** — shift-modified punctuation is dropped. No
  keysym variant works (`colon`, `shift-colon`, `shift-semicolon`, raw `0x3a`). This is
  what blocked Recovery Console for so long; don't reintroduce a dependency on it.
- **VMware Tools for XP** — the shipped `windows-x86.iso` is 12.4.8 and its MANIFEST
  lists only `_Win7`/`_Win8` drivers. `vmrun runProgramInGuest` is unavailable.
- **`bios.bootOrder` in the `.vmx`** — ignored by Workstation/Player. To force CD boot,
  detach the hard disk or catch the prompt by polling screenshots.
- Full razzle build of *everything* — the kernel subtree builds; whole-tree still needs
  pieces not in the leak.

---

## Part 2 — Milestones

### M0 — Prove the code compiles at all  ✅ **DONE**

Superseded and exceeded. We did not need a mingw-w64 port to answer the question: the tree
ships its own MSVC toolchain, and it runs under Wine. The whole kernel subtree compiles and
links, giving `ntoskrnl.exe` with a matching 1,464-export surface.

**Gate result:** a kernel built entirely from this tree's source **boots a full XP install**.
Evidence: `docs/KERNEL-BOOTS-XP.md` (screenshots, MD5s, procedure).

The user-mode port probe (`admin/cmdline/eventcreate`) is no longer on the critical path and
remains only as a demo of the mingw route; `harness/xpbuild.py` covers that ground.

<details><summary>Original M0 plan, kept for reference</summary>

**Probe target chosen:** `admin/cmdline/eventcreate` — 1,190 LOC C, single translation unit,
Win32-only (`windows.h`, `stdio`, `tchar`), no kernel/win32k headers. Dependencies:
`cmdline.lib` (in-tree), `user32, mpr, secur32, ws2_32, netapi32` (all present in Windows SDK form).

**Steps**
1. Read `sources` → extract the exact `C_DEFINES`, libs, entry point (`wmain`, console).
2. Port the build description to a modern toolchain (CMake or plain Makefile + mingw-w64).
3. Compile and record **every** distinct error class (SEH, `__declspec`, pragma, missing header, etc.).
4. Fix the mechanical ones; leave the semantic ones documented.
5. Link and run under Wine; confirm the binary produces its usage text.

**Gate:** a PE binary built from unmodified-ish MSVC-6 source that runs under Wine.
**If this fails on fundamentals:** stop, report, re-scope — do not start M1.

**Time:** 1–2 focused sessions. **Risk:** high (this is the honest make-or-break).

</details>

---

### M1 — x64 direction (kernel-first, never UI-first)

Only meaningful after M0 — which has now passed, so this is unblocked. **Measured, not
assumed: see `docs/X64-STATUS.md`.**

**The finding that changes this milestone's shape:** the tree contains a complete AMD64
cross-compiler at `tools/win64/x86/amd64/` (`cl.exe`, `link.exe`, `ml64.exe`, `lib.exe`),
and razzle already supports the target (`tools/makefile.plt`: `TARGET_DEFINES=-D_AMD64_
-D_WIN64`). x64 is **a build flag that was never turned on**, not a porting research
project.

**Result of turning it on: 21 of 23 kernel subsystems build clean for x64.** The RTL
produces a real 4.7 MB `ntosrtl.lib` with COFF machine type `0x8664`.

**What actually blocks an x64 kernel link:** exactly two source files,
`base/ntos/ex/exinit.c` and `systime.c`, were never included in the leak. For i386 and
ia64 they are masked by prebuilt objects; no `obj/amd64/` equivalents exist. They are
48 named functions, with signatures recoverable from the i386 objects. `ex` is a
prerequisite for the kernel link, so this is the current gate.

Three real defects were fixed to reach 21/23: missing `STATUS_*` equates in
`kxamd64.inc`, the MASM hex-literal rule (`C0000005H` parses as a symbol; needs a
leading zero), and a `const` on `MmPagedPoolStart` that contradicts an assignment in
`mminit.c`. Details and repro commands in `docs/X64-STATUS.md`.

**Known state (corrected):** the old note here said `base/ntos/mm/amd64` was a 4-file stub
and there was no HAL for x64. Both were wrong: the kernel carries **71 amd64 files vs 141
i386**, and `base/hals/halacpi/amd64/` has 30 files while `base/hals/x86new/amd64/sources`
declares 24. Neither HAL has been built yet — that is the next measurement, not an unknown.

**Steps**
1. ~~Pick one unit with real amd64 code~~ — done, and far beyond it: 21 subsystems build.
2. Reconstruct `exinit.c` and `systime.c` for x64 (48 functions). This is the gate on
   linking a kernel image. Signatures come from the prebuilt i386 objects and from the
   prototypes exported in `base/ntos/init/ntoskrnl.src`; behaviour must be reproduced
   from the i386 disassembly with REA+Ghidra. `ExpInitSystemPhase0/1` and
   `ExpSetSystemTime` are the large ones — budget accordingly.
3. Build the x64 HAL (`base/hals/halacpi/amd64`, 30 files). Never attempted.
4. Link an x64 `ntoskrnl.exe` and compare its export surface to the 1,464 the x86 build
   produces. The `amd64def.src` forced-inline export list already exists for this.

**Gate:** a 64-bit PE kernel image that links, with a written list of what is still stubbed.
**Explicitly out of scope at M1:** win32k x64, shell x64, booting. Booting x64 additionally
needs an entirely separate boot path (`ntldr` → x64 osloader, x64 HAL, x64 drivers).

**Risk:** very high. **This is a multi-month milestone, not a sprint.**

---

### M2 — Performance  ⟵ **current**

Highest value per hour, zero visual risk, and the only goal that needs no porting. The
prerequisite — *a build that actually runs* — now exists, so this is the live track.

**Measurement baseline now available.** `xp.vmx` boots our kernel to a desktop, and the
whole VM is driven headlessly: `vmrun start … nogui`, VNC capture via
`vncdo -s 127.0.0.1::5901 -p xpvm capture out.png`, keystrokes via `vncdo type`. Every perf
change can be A/B'd by swapping one kernel file into the VMDK and booting again
(`tools/ntfspatch.py`). Keep `xp-pristine.vmdk` as the Microsoft-kernel control.

**Targets already identified in-tree**

| Area | Where | What to change |
|---|---|---|
| GDI batching / lock contention | `windows/core/ntgdi/Engine.c` | per-call critical sections around batch ops |
| Scheduler quanta + priority boost | `base/ntos/inc/exlevels.h`, `exboosts.h` | constants sized for 128 MB machines |
| Working-set / balance-set trim | `base/ntos/mm` | trim thresholds far too aggressive on 8+ GB |
| Explorer startup polling | `shell/explorer` (tray, startmnu, trayclok) | hard-coded poll intervals |

**Steps**
1. ~~Establish a baseline~~ — baseline is our kernel in `xp.vmx`; control is
   `xp-pristine.vmdk` (Microsoft's kernel). Same disk, same VM, one file differs.
2. One change at a time, each with a measurable before/after.
3. Pixel-identity check for anything touching ntgdi/ntuser/theme (feeds M4).

**Gate:** a measured improvement on a stated workload, with UI screenshots byte-identical —
*against the Microsoft kernel*, since both now boot on identical hardware.
**Risk:** low. **Blocked on:** nothing.

---

### M3 — "Remove Microsoft proprietary code" (reframed)

The tree contains **no third-party blob**; it is all Microsoft source. So this goal means
**replacing a subsystem with an open implementation**, one at a time.

**Candidate order (small → large):**
1. `dns` resolver (`net/dnsrslvr` or equivalent) — small, well-specified protocol.
2. `crypt32` / crypto providers — medium, replaceable with OpenSSL/OpenBSD-style code.
3. Network stack (`net/tcpip`) — large, ROS/LWIP-style replacement.
4. Never: win32k, ntgdi (would change appearance — violates the hard constraint).

**Gate:** the replacement passes the original subsystem's own tests, and no UI change is observed.
**Risk:** high, and it is the *last* goal on this list, not the first.

---

### M4 — Appearance guarantee harness (cross-cutting, can start now)

The constraint "don't change the looks" needs an automated check, not good intentions.
With two bootable kernels on the same disk, this is a straight image diff — no longer
hypothetical.

**Steps**
1. Choose a fixed set of screens (desktop, start menu, My Computer, a control panel page, GDI text sample).
2. Capture reference screenshots from an unmodified XP SP1 VM.
3. After every M2/M3 change, re-capture and diff.

**Gate:** a diff harness that can fail. **Risk:** low, high payoff. **Unblocked:** our kernel
boots, so reference-vs-ours captures are both obtainable from `xp.vmx`.

---

## Part 3 — Order of work

```
M0 build+boot ✅
   │
   ├──► M2 perf  ⟵ current   ──► M4 harness (parallel)
   │
   └──► M1 x64 skeleton ──► M1 partial kernel ──► (long horizon)
                                    │
                                    └──► M3 subsystem replacement (last)
```

## Part 4 — Decisions already settled

1. ~~Goal split~~ — accepted: appearance = free; perf = achievable; x64 = port, not a flag;
   "de-Microsoft" = subsystem replacement.
2. ~~Probe target~~ — moot. The tree's own toolchain answers M0 directly.
3. ~~Toolchain for M0~~ — **the tree's own MSVC under Wine**, not mingw. This kills the
   `C_ASSERT`/struct-size blocker by construction.
4. ~~Is a VM available~~ — yes, `~/vms/xpvmware/xp.vmx`, driven headlessly over VNC.
5. **Still open:** whether to keep pursuing a mingw-w64 source port at all. The razzle route
   builds the kernel today, but it depends on Wine and a leak-era compiler — fine for
   *measuring* and *patching*, not obviously fine as the long-term modern toolchain for x64.

## Part 5 — Honest expectations

| Goal | Realistic outcome |
|---|---|
| Identical looks | Keepable — nothing requires touching rendering. |
| Faster | Genuinely achievable on the paths listed, verifiable. |
| x64 | **Revised.** The toolchain is in the tree and 21/23 kernel subsystems already compile for x64. The blocker is two missing source files (48 functions). Linking an x64 kernel is a bounded task, not a research program. |
| No MS code | Means rewriting subsystems; achievable one subsystem at a time, not wholesale. |
| Fully booting modern XP | ~~Not realistic~~ — **already done** for the kernel. It builds and boots a complete XP SP1 install. What the tree still lacks is everything *above* the kernel: a full user-mode/shell link and a driver set. |
