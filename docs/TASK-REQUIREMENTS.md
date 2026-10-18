# What this task actually requires — tool and skill survey

Written before any replacement code is started. The point is to find out whether the
work is possible here, not to assume it.

Verified on this machine, 2026-10-18. Every ✅ below was executed, not inferred.

---

## 1. The toolchain question is solved

The riskiest assumption — *can modern C23/C++23 code even become an XP kernel binary?* —
turned out to be a yes, tested end to end.

```
clang 23.1.3 (llvm-mingw)  ->  x86-64 COFF object
                           ->  MS link.exe (tree's own, under Wine)
                           ->  PE32+ executable for MS Windows 5.01 (native), x86-64
```

That 1024-byte test binary is not a mockup: a clang-produced freestanding object was
linked by the tree's own AMD64 `link.exe` into a native x86-64 PE. **Modern compiler,
legacy linker, working output.** Everything downstream depends on this and it holds.

| Tool | Version | Status |
|---|---|---|
| clang / clang++ | **23.1.3** | ✅ full `-std=c23` and `-std=c++23` |
| clang-tidy | 23.1.3 | ✅ present in the llvm-mingw tree |
| clang-format | 23.1.3 | ✅ |
| clangd (LSP) | 23.1.3 | ✅ |
| scan-build-py / analyze-build | 23.1.3 | ✅ |
| gcc / g++ | 14.2.0 | ✅ host-side checks |
| system clang | 19.1.7 | ✅ carries the Linux sanitizer runtimes |
| cmake / ctest | 3.31.6 | ✅ |
| libFuzzer + ASan/UBSan | clang 19 | ✅ **verified by finding a deliberate crash** |
| gdb, objdump, readelf | system | ✅ |

**Consequence:** replacements can be written to C23 / C++23, statically analysed,
sanitised and fuzzed — all locally, no `sudo`, no downloads.

### The one real toolchain constraint

`clang --target=x86_64-windows-msvc -ffreestanding` produces objects for ring 0. But the
**MSVC 13.00 compiler in the tree predates C++11 by nine years**. So:

- Replacement units are compiled by **clang**, not the leak's compiler.
- They link with the tree's `link.exe`, which is fine — the ABI is C.
- The two toolchains must agree on struct layout, calling convention and name mangling.
  They do for `extern "C"`; that is exactly why the replacement boundary has to be C.

---

## 2. The verification story is the strong half

This is where the project is unusually well equipped, and it is the part that makes
"replace a subsystem" tractable at all.

| Asset | What it gives us |
|---|---|
| `xp-pristine.vmdk` | Microsoft's kernel, bootable — **the control** |
| `xp.vmdk` | our kernel on the *same disk*, one file different — **the subject** |
| `tools/ntfspatch.py` | swap a binary host-side, no root, no VMware Tools |
| `vmrun` + `vncdo` (+ `pexpect`) | drive the guest headlessly, capture screens, type commands |
| VNC on `127.0.0.1:5901` | automated observation |

So an A/B oracle exists today: boot control, record behaviour, boot subject, diff.
That is rare and it is the reason the replacement plan is credible rather than hopeful.

**Gaps in the oracle (real, must be closed):**
- No conformance *runner*. `vncdo` is manual-ish; there is no harness that boots a VM,
  runs N tests inside XP, collects results, and reports pass/fail.
- No binary diffing for behaviour comparison (only `objdump`; no radiff2/BinDiff).
- Ghidra 12.1.4 is installed but `GHIDRA_INSTALL_DIR` is unset and REA is not wired to
  this task yet.
- No CI, no git hooks beyond the 14 stock templates.

---

## 3. Skills this needs — honest assessment

Five distinct kinds of work. They are not the same skill and they are not equally covered
by what we have or by me.

### 3.1 Writing modern, correct C/C++ — **covered**
C23/C++23, freestanding discipline, RAII where it helps, `std::span` over pointer+len.
Tooling above supports this fully.

### 3.2 Kernel-mode constraint discipline — **partially covered**
Ring 0 means: no exceptions, no RTTI, no allocating `std::` containers, no CRT, custom
pool allocation, IRQL awareness, no paged memory at DISPATCH_LEVEL.

The precedent is in-tree: win32k ships **70 `.cpp` files** and **none of them use
`try`/`catch`/`dynamic_cast`/`typeid`**. So the rules are demonstrated, not invented.
But knowing *which* rules apply to *which* function is expertise gained by breaking things.

### 3.3 Protocol/format specification literacy — **covered by external material**
DNS = RFC 1035/2181/6891. DHCP = RFC 2131/2132. Crypto = NIST FIPS 180-4, RFC 8017.
This is the work most amenable to doing well here, because the spec is authoritative and
the test vectors are published.

### 3.4 Differential testing against a real system — **skill gap, highest risk**
Making XP and a replacement produce comparable observations is genuinely hard: you need
to capture packets, registry states, error codes, timing, and malformed-input behaviour,
deterministically, from a GUI OS with no automation agent. This is where most of the
project's engineering effort will actually go, and it is the part least served by
existing tools.

### 3.5 Legal hygiene — **not a skill I should be the authority on**
I can enforce discipline (clean provenance, specs not source, no transcription) and write
the notices. I cannot give you legal advice, and the derivative-work question around
"modernising Microsoft's C" is exactly the kind of thing that needs a lawyer, not an
engineer. Flagging that now, not later.

---

## 4. What is missing, ranked by how much it blocks

| # | Gap | Blocks | Fixable here? |
|---|---|---|---|
| 1 | **Conformance runner** — boot VM, run tests, collect results | everything in R2–R4 | ✅ yes, python + vmrun/vncdo |
| 2 | **A LICENSE / NOTICE** — none exists | any public contribution | ✅ yes, an hour |
| 3 | **ABI-compat check** — clang vs MSVC struct layout & calling convention | every replacement | ✅ yes, static_assert matrix |
| 4 | **Behaviour capture** — packets, registry, error codes from XP | differential tests | ✅ yes, in-guest helper binary |
| 5 | **Mutation testing** — does the suite actually catch bugs? | trusting the tests | ⚠️ no tool installed; small script |
| 6 | Binary diffing for behaviour recovery | R3/R4 | ⚠️ no radiff2/BinDiff |
| 7 | Ghidra/REA not wired | understanding existing behaviour | ✅ config exists, needs setup |
| 8 | No CI | long-term regression | ⚠️ local only, no hosting assumed |

**The headline: gap 1 is the real work.** Not writing the DNS codec — writing the machine
that tells you the DNS codec is right. Anyone can write a resolver; the value here is the
oracle that proves it.

---

## 5. Recommended order, given this survey

1. **Prove the ABI boundary** (gap 3). Write one clang-built C23 object that exports a
   `__stdcall` function with a packed struct, link it with the tree's `link.exe`, call it
   from the existing kernel build, and confirm struct offsets match between clang and the
   in-tree MSVC. If this fails, everything downstream is built on sand. It is a day's work
   and it de-risks the whole plan.
2. **Build the conformance runner** (gap 1) against a known-good target first — run it on
   `xp.vmdk` vs `xp-pristine.vmdk` with the kernels we already have. It must report
   *identical*; if it reports a difference, the runner is wrong.
3. **Then** the first replacement (R1 pure algorithms), because it needs the runner and
   gives the runner something real to validate against.
4. LICENSE/NOTICE (gap 2) before anything goes public.

---

## 6. The honest summary

**Possible?** Yes, for the well-specified subsystems, and the toolchain question is
settled in our favour.

**What makes it likely to succeed here:** the oracle. Two bootable kernels, one disk, and
a root-free way to patch binaries. That is the hard infrastructure and it exists.

**What makes it likely to fail:** the differential-testing engineering (3.4), which is
unglamorous, fiddly, and where the actual months go. Also: scope. Crypto + DNS + DHCP +
TCP/IP is ~5% of NT by line count. The kernel proper has no external spec and cannot be
tackled this way — ReactOS is the answer there and it is GPL/LGPL.

**What I would not start without:** gap 1 and gap 3. Writing replacement code before the
runner exists means writing code you cannot prove is correct, which is how these projects
die at 4% complete.
