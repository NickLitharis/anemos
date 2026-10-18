# G1 — ABI compatibility proof

**Verdict: PASS, 15/15 checks, 2026-10-18.** clang 23.1.3 and the tree's MSVC 13.00
(`Source/XPSP1/NT/tools/x86/cl.exe`, `Version 13.00.9176`) agree on struct layout,
calling convention and name decoration for the x86 (i386) boundary. Every replacement
in P1+ may therefore be written in C23 by clang and called by MSVC-built code.

Reproduce:

```sh
tools/abi/run.sh          # exit 0 = G1 passes
```

The script builds both toolchains' halves, links them into each other, runs what can be
run under Wine, and diffs. It exits non-zero on any disagreement.

---

## What was tested, and why that shape

The question G1 exists to answer: *if clang writes a structure and MSVC reads it — or the
reverse — do they see the same bytes and the same stack?* The answer is measured, not
argued, by four checks, each of which can fail.

| Check | Question | Method |
|---|---|---|
| **A layout** | same declaration → same `sizeof`/`offsetof`? | identical source built by both compilers, both executables run under Wine, output diffed |
| **B decoration** | same function → same symbol name and argument byte count? | `llvm-nm` on both objects, defined symbols compared |
| **C cross-call** | can each call the other's functions and read the other's memory? | clang callee + MSVC caller, **and** the reverse; every field checked at runtime |
| **D kernel lib** | does a clang object find XP's own RTL? | clang object linked by the tree's `link.exe` against real `ntosrtl.lib`, `/nodefaultlib`, native subsystem |
| **control** | can the harness detect a failure at all? | one layout value perturbed; the diff must report it |

The negative control matters as much as the checks: a runner that reported "identical"
on perturbed data would make every PASS above worthless.

### Evidence

Check B — defined symbols, both objects byte-identical:

```
_abi_deco_byval12@16        _abi_deco_packed@8
_abi_deco_byval8@12         _abi_deco_ret12@4
_abi_deco_cdecl             _abi_deco_stdcall@16
@abi_deco_fastcall@8        _abi_deco_global
                             _abi_deco_rodata
```

`@16` on a 12-byte struct argument plus a 4-byte `int`, `@12` on an 8-byte struct plus an
`int`, `@4` on a struct return whose hidden pointer is counted — the stack byte count is
computed identically by both compilers, so a layout disagreement would already have shown
up here as a link error, before anything ran.

Check A — 56 measurements, identical, excerpt:

```
sizeof abi_t_simple           24        sizeof abi_t_p1              21
offset abi_t_simple  q        16        offset abi_t_p1  i            1
sizeof abi_t_bits_mixed        2        offset abi_t_p1  q            5
sizeof abi_t_aligned          16        sizeof abi_cross_packed      30
offset abi_t_aligned c         4        offset abi_cross_packed ratio 22
```

Check C — both directions, all field checks:

```
CROSS: all checks passed      (clang callee, MSVC caller)
CROSS: all checks passed      (MSVC callee, clang caller)
```

Check D — `tools/abi/out/kernellink.sys`, PE32 native i386, 2,560 bytes, built by
clang + tree's `link.exe` + real `ntosrtl.lib`, references resolving to the library's
own `_RtlInitString@8` / `_RtlInitAnsiString@8`.

Coverage of the layout matrix: default 8-byte packing, `#pragma pack(1)` and `pack(4)`,
nested structs, unions, arrays, pointers and function pointers (stdcall), `__int64`,
`double`, `long double`, enums, `__declspec(align(16))`, three bitfield shapes
(`unsigned`/`unsigned char`+`char`/`int`+`long`), packed cross-TU structs, 8-byte and
12-byte structs passed and returned by value.

---

## Findings P1+ must respect

1. **clang objects carry no `/DEFAULTLIB` directive.** MSVC objects declare their own CRT
   inside the object; clang's do not, so linking a clang object with `cl.exe` needs the
   libraries named explicitly (`libcmt.lib oldnames.lib`). Measured: without them the link
   fails on `_printf` and `_mainCRTStartup`. Kernel code is unaffected — it links
   `/nodefaultlib` anyway.
2. **MSVC C wants the calling convention inside the declarator.** `typedef unsigned
   __stdcall (*fp)(int);` is a syntax error in cl 13.00; `typedef unsigned (__stdcall
   *fp)(int);` compiles in both. Headers shared with the tree must use the second form.
3. **Bit-fields have no address.** clang refuses `offsetof` on a bit-field (C23: it is not
   a declared object); measure `sizeof` of the containing struct instead, which is where a
   placement disagreement surfaces anyway.
4. **Mixing the toolchains in one image produces two harmless warnings:** `LNK4078`
   (`.rdata` sections carry different attributes) and `LNK4098` (`LIBC` vs the explicitly
   named `LIBCMT`). Neither affects correctness; both are expected in every mixed link.

## What this does not prove

- **AMD64.** This proof is i386. The x64 story needs the same treatment against the tree's
  x64 tools before any x64 replacement work starts.
- **Layout of Microsoft's own structures.** The matrix measures the *layout rules* on
  declarations both compilers see; it does not (and legally should not) include the
  leak's headers.
- **Runtime equivalence of anything we write.** That is G2's job, with the negative
  control: the same runner pointed at `xp.vmdk` and `xp-pristine.vmdk` must report
  *identical*.

## Files

```
tools/abi/run.sh            driver; exit 0 = G1 passes
tools/abi/abi_types.h       the shared declaration matrix (C89-parseable for cl 13.00)
tools/abi/layout_probe.c    check A   tools/abi/decor_probe.c    check B
tools/abi/cross_callee.c    check C   tools/abi/cross_main.c     (both sides built twice)
tools/abi/kernellink_probe.c check D
tools/abi/*.cmd             MSVC-side build steps, run under Wine in the tree's environment
```
