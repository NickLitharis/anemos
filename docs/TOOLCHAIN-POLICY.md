# Toolchain policy — newest compiler, newest linker

Pinned deliberately. An OS lives for decades and its compiler outlives several hardware
generations, so "whatever is installed" is not good enough: the versions are recorded here
and changes to them are a reviewed decision, not a side effect.

Verified on this machine, 2026-10-18. Every ✅ below was executed.

---

## 1. The pinned toolchain

| Component | Version | Binary | Why this one |
|---|---|---|---|
| **C compiler** | **clang 23.1.3** | `llvm-mingw-20261006/…/clang` | latest released LLVM |
| **C++ compiler** | **clang++ 23.1.3** | same tree | same frontend, one version for both |
| **Linker** | **lld-link 23.1.3** | same tree | modern, no Wine, no MS linker |
| **Archiver** | **llvm-ar 23.1.3** | same tree | reads MS `.lib` archives |
| **Static analysis** | clang-tidy 23.1.3 | same tree | matches the compiler exactly |
| **Formatter** | clang-format 23.1.3 | same tree | |
| **LSP** | clangd 23.1.3 | same tree | |

`llvmorg-23.1.3` was released **2026-10-06**; it is the current upstream release, confirmed
against the LLVM releases API. We are on it — the version string matches upstream exactly
(`0d261d1ca552c95a8f007e061c787ac7132fbcbc`).

**Nothing in the deliverable is built by a pre-2010 compiler.**

## 2. The conflict this resolves

The tree ships its own toolchain — and it is 24 years old:

```
Microsoft (R) C/C++ Optimizing Compiler Version 13.00.9269.1 for AMD64   (2001)
Microsoft (R) Incremental Linker Version 7.00.9269.1                     (2001)
```

That toolchain is how the **existing x86 kernel builds and boots** (`docs/KERNEL-BOOTS-XP.md`),
and it stays for that purpose: it is the only thing that compiles the Microsoft source, and
the boot result is the project's headline evidence. Do not disturb it.

But it must not produce Anemos code. Two toolchains, two jobs:

| Job | Toolchain | Reason |
|---|---|---|
| Build the legacy XP kernel (oracle, evidence) | in-tree MSVC 13.00 under Wine | only thing that compiles it |
| **Build all Anemos code** | **clang/lld 23.1.3** | modern, no Wine, analysis-capable |

## 3. Proof that lld-link replaces the 2001 linker

The load-bearing question was whether the modern linker can consume the tree's real
archives. Tested, not assumed:

```
lld-link 23.1.3  +  clang -Oz object  +  ntosrtl.lib (4.7 MB, amd64 objects)
                 ->  PE32+ native x86-64, 6144 bytes
```

And the linked code is genuinely XP's, not a stub. Disassembling the output:

```
140004010:  69 c0 ed ff ff 7f   imul $0x7fffffed,%eax,%eax
140004016:  05 c3 ff ff 7f      add  $0x7fffffc3,%eax
140004023:  f7 f3               div  %ebx
```

That is `RtlRandom`'s LCG from XP's own `random.c`, pulled out of `ntosrtl.lib` and linked
by a 2026 linker into a working x64 executable.

So: **modern compiler → modern linker → real kernel libraries → working PE.** No Wine in
the Anemos build path.

## 4. Required flags

```bash
CC   = clang   --target=x86_64-windows-msvc -mno-red-zone -mcmodel=kernel \
               -ffreestanding -fno-builtin -fno-stack-protector \
               -fno-exceptions -fno-rtti -fno-threadsafe-statics \
               -std=c23 -Oz -Wall -Wextra -Werror -Wconversion -Wshadow \
               -Wcast-align -Wstrict-prototypes -Wmissing-prototypes \
               -Wnull-dereference -fno-omit-frame-pointer
CXX  = clang++ (same, -std=c++23, plus -fno-use-cxa-atexit -fno-rtti)
LD   = lld-link /nologo /subsystem:native /nodefaultlib /opt:ref /opt:icf \
               /debug:none /dynamicbase /nxcompat
               # /guard:cf omitted -- needs _load_config_used, see section 4
```

`-mcmodel=kernel` matters: kernel code sits in the upper half of the address space and the
default small code model will not reach it.

`/dynamicbase` and `/nxcompat` work as advertised and are verified set in the PE header
(`DllCharacteristics` 0x8160: ASLR ✔, NX ✔). Use both.

**`/guard:cf` does not currently work and must not be claimed.** Tested: passing the flag
sets the `GUARD_CF` bit in `DllCharacteristics`, but emits

```
lld-link: warning: Control Flow Guard is enabled but '_load_config_used' is missing
```

and inspection of the output shows the Load Config data directory at `rva=0x0 size=0` —
**no load config exists, so the CFG bitmap and function table are absent and the flag
enforces nothing.** It is a bit in a header, not a mitigation.

To get real CFG a freestanding kernel must supply its own `_load_config_used` structure
(through `GuardFlags` at offset 0x70) plus a `GuardCFFunctionTable` of valid indirect-call
targets. Hand-building that table for a kernel with dispatch tables is real work, and it
belongs to P4 alongside the indirect-call surface, not to a build flag.

Until then: `/guard:cf` is omitted rather than included-and-ineffective. A security flag
that silently does nothing is worse than an absent one, because it stops anyone looking.

## 5. Why not the in-tree compiler

Beyond the obvious (it predates C11 by a decade), three concrete reasons:

1. **No modern C at all.** MSVC 13.00 has no `_Static_assert`, no `[[nodiscard]]`, no
   `std::span`, no concepts. The coding rules depend on these.
2. **Its `cl.exe` only runs under Wine.** That is a fragile dependency for a build that
   must be reproducible by contributors.
3. **No usable static analysis.** lld 23's `clang-tidy` and `scan-build` are integral to
   `OS-CODING-RULES.md` §10; the 2001 toolchain has no equivalent.

## 6. Why not gcc

`gcc 14.2` is installed and fine, but:
- its Windows target (`mingw-w64`) and the MS ABI for `__stdcall`/`__fastcall` on x64 are
  less precise than clang's `--target=x86_64-windows-msvc`;
- `clang-tidy` cannot analyse GCC's output;
- one frontend for both C and C++ keeps diagnostics consistent.

GCC stays available for host-side verification (that is where `-fsanitize=fuzzer` works —
the mingw tree carries no Linux runtimes).

## 7. Verification, run this when anything changes

```bash
export PATH=$HOME/.local/opt/llvm-mingw-*/bin:$PATH

clang --version | head -1          # must be 23.1.3
lld-link --version                 # must be 23.1.3

# the interop test that matters
clang --target=x86_64-windows-msvc -mcmodel=kernel -ffreestanding -Oz -c t.c -o t.obj
lld-link /nologo /entry:entry /subsystem:native /nodefaultlib /out:t.exe t.obj
file t.exe                         # must say "PE32+ ... x86-64"
```

If upstream releases 24.x, bumping is a deliberate commit that re-runs §7 and the
conformance suite — not an automatic anything.

## 8. Recorded versions are part of the build

`tools/toolchain.lock` pins them in machine-readable form so a build can refuse to run on
the wrong compiler rather than producing subtly different output:

```
clang 23.1.3
lld-link 23.1.3
```

Reproducibility is the point: a kernel that builds differently depending on what is
installed is not buildable, it is merely compilable.
