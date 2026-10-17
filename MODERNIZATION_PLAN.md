# XP SP1 Modernization — Master Plan

Tree: `/home/marocos/winxp_source/Source/XPSP1/NT` — 67.8M LOC, 277,865 files, commit `a2f6d7c93`.
Goals, in the user's words: **x64 · faster · no Microsoft-proprietary code · identical looks.**

This plan is ordered by *dependency*, not by ambition. Each milestone has a
**gate**: a check that can fail. A milestone is not done until its gate passes.

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
| x64 code present | **151** `.c/.h/.asm` total, only in `base/boot/lib` + `base/crts/crtw32` shims |

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
| wine | system | ✅ runs PE32 |
| gcc 14.2 | system | ⚠️ Linux ELF only, no Windows headers |
| mingw-w64 | — | ❌ not installed (`sudo apt install gcc-mingw-w64-i686`) |
| REA + Ghidra | — | ❌ M1 tooling |
| `legacy-nt-modernization` skill | `~/.pi/agent/skills/` | ✅ loaded (census baked in) |

### 1.6 Known dead ends (do not retry)

- 8 LFS pointers (`enduser/speech/tts/truetalk/voices/*/*.sfont`) — 404 server-side, permanent.
- Full razzle build — impossible without MSVC 6 + DDK.
- A 34B-class local model — 15.5 GiB RAM, no GPU; needs ≥40 GiB.

---

## Part 2 — Milestones

### M0 — Prove the code compiles at all  ⟵ **current**

Nothing downstream is real until this passes. Everything else is speculation.

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

---

### M1 — x64 direction (kernel-first, never UI-first)

Only meaningful after M0. Focus on the smallest truthful step.

**Known state:** `base/ntos/mm/amd64` = 4 files (`dataamd.c`, `initamd.c`, `miamd.h`, `sources`).
That is a stub. `nical`/`halia64` exist; nothing x64 for HAL.

**Steps**
1. Install mingw-w64 x86_64 + Ghidra + REA (M1 tooling).
2. Pick **one** unit with real amd64 code already in-tree (`base/ntdll/amd64` or `base/boot/lib/amd64`)
   and get it to compile 64-bit first. Smallest possible win.
3. Inventory `mm/amd64` against `mm/i386` symbol-by-symbol using the tags index; write the
   missing-function list.
4. Use REA+Ghidra on a reference AMD64 NT binary only for functions with no source analogue.
5. Produce a 64-bit `ntoskrnl` skeleton that links (does not need to boot).

**Gate:** a 64-bit PE kernel image that links, with a written list of what is still stubbed.
**Explicitly out of scope at M1:** win32k x64, shell x64, booting.

**Risk:** very high. **This is a multi-month milestone, not a sprint.**

---

### M2 — Performance (do this in parallel with M0 once *any* build exists)

Highest value per hour, zero visual risk, and the only goal that needs no porting.

**Targets already identified in-tree**

| Area | Where | What to change |
|---|---|---|
| GDI batching / lock contention | `windows/core/ntgdi/Engine.c` | per-call critical sections around batch ops |
| Scheduler quanta + priority boost | `base/ntos/inc/exlevels.h`, `exboosts.h` | constants sized for 128 MB machines |
| Working-set / balance-set trim | `base/ntos/mm` | trim thresholds far too aggressive on 8+ GB |
| Explorer startup polling | `shell/explorer` (tray, startmnu, trayclok) | hard-coded poll intervals |

**Steps**
1. Establish a baseline first: which build do we measure? (needs M0 output)
2. One change at a time, each with a measurable before/after.
3. Pixel-identity check for anything touching ntgdi/ntuser/theme.

**Gate:** a measured improvement on a stated workload, with UI screenshots byte-identical.
**Risk:** low. **Blocked on:** M0.

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

### M4 — Appearance guarantee harness (cross-cutting, start early)

The constraint "don't change the looks" needs an automated check, not good intentions.

**Steps**
1. Choose a fixed set of screens (desktop, start menu, My Computer, a control panel page, GDI text sample).
2. Capture reference screenshots from an unmodified XP SP1 VM.
3. After every M2/M3 change, re-capture and diff.

**Gate:** a diff harness that can fail. **Risk:** low, high payoff. **Can start now** (needs a VM, not our build).

---

## Part 3 — Order of work

```
M0 probe ──► M2 perf (once any build runs) ──► M4 harness (parallel, needs VM only)
   │
   └──► M1 x64 skeleton ──► M1 partial kernel ──► (long horizon)
                                    │
                                    └──► M3 subsystem replacement (last)
```

## Part 4 — What must be decided before M0 starts

1. **Accept the goal split?** Appearance = free; perf = achievable; x64 = port, not flag; "de-Microsoft" = subsystem replacement.
2. **Probe target confirmed?** `admin/cmdline/eventcreate` (Win32 console tool, 1 unit, no kernel deps).
3. **Toolchain choice for M0:** mingw-w64 (needs `sudo apt install`) vs. LibreOffice/LLVM-cl-based route.
4. **Is a Windows XP VM available** for the M4 appearance harness?

## Part 5 — Honest expectations

| Goal | Realistic outcome |
|---|---|
| Identical looks | Keepable — nothing requires touching rendering. |
| Faster | Genuinely achievable on the paths listed, verifiable. |
| x64 | Years of work at full effort; first useful milestone is a linking skeleton. |
| No MS code | Means rewriting subsystems; achievable one subsystem at a time, not wholesale. |
| Fully booting modern XP | Not realistic from this tree — it lacks a complete build environment. |
