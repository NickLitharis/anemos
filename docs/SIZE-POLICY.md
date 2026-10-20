# Size policy — keeping the output small

Size is a design constraint here, set deliberately before any code exists. This document
records the measured levers, the ones that turned out to be myths, and the rules that
follow.

Every number below was produced on this machine with clang 23 targeting
`x86_64-windows-msvc`, and where stated linked with the tree's own `link.exe`.

> **Artifact note:** the `dist/kernel/` binaries referenced below are
> Microsoft-derived and are kept locally in `~/anemos-private/dist/`; they are
> not part of this repository.

---

## 1. What "small" competes with

Size is not free. These are the trade-offs we are accepting, stated so they are not
discovered later:

| Choose | Cost |
|---|---|
| Small | Some speed; `-Oz` over `-O2` measured **2.6× smaller** on a realistic module (937 vs 2414 bytes) |
| Compatibility | The Windows ABI fixes struct layouts and export tables — those bytes are not ours to remove |
| The oracle | Keeping debug info in *test* builds is worth size; ship builds differ from test builds |
| Legibility | Aggressive macro tricks to save bytes violate `OS-CODING-RULES.md` §17 (preprocessor limits) |

Rule: **never trade correctness or ABI compatibility for bytes.** A kernel that saves 4 KB
and mis-parses one struct is not smaller, it is broken.

## 2. Measured levers, in order of effect

### 2.1 `-Oz` — the single biggest lever

On a realistic module (CRC-32 with table init):

| Flag | Object size |
|---|---|
| `-O2` | 2414 b |
| `-Os` | 940 b |
| **`-Oz`** | **937 b** |

`-Os` and `-Oz` are within 0.3% of each other at this size; `-Oz` is chosen for
consistency, not because it beat `-Os` meaningfully.

### 2.2 Where the bytes actually go

Section breakdown of that `-Oz` object:

```
.text          197 b    <- the actual code
.data            0 b
.bss          1040 b    <- uninitialised, does not occupy the file
.debug$S       156 b    <- debug metadata
```

**The code is 197 bytes.** Most of the object is debug metadata and BSS accounting.
This generalises: in a kernel, *file size* is dominated by data and debug sections, not
instruction selection. So tuning `-O` level past `-Oz` is low-value; controlling what
gets linked in is high-value.

### 2.3 Our real kernel, for scale

`dist/kernel/ntoskrnl.exe` as built — the x86 boot kernel, PEI-I386, 21 sections,
1,915,890 bytes of section data:

| Section | Bytes | % |
|---|---|---|
| PAGE | 880,294 | 45.9% |
| .text | 383,328 | 20.0% |
| INIT | 162,770 | 8.5% |
| .rsrc | 83,048 | 4.3% |
| .data | 75,172 | 3.9% |
| .reloc | 62,544 | 3.3% |

**Only 20% is code.** `PAGE` (pageable) and `INIT` (discardable after boot) together are
54%. That is where size work pays: moving cold code into `INIT` and hot-but-rare code
into `PAGE` costs nothing at runtime and removes bytes from the working set.

## 3. Levers that turned out to be myths

Measured, not assumed. These are commonly recommended and did **not** help here.

**`-ffunction-sections -fdata-sections`** — made the object *larger* (827 b vs 675 b) and
changed the linked output not at all (both 1024 b). clang already emits unreferenced
`static` functions as discardable, and the linker drops them either way.

**`/opt:ref` as a dead-code lever** — verified it removes nothing extra: a binary built
*without* it contained zero of the 100 unreferenced `unused_N` functions. The compiler had
already discarded them. Keep `/opt:ref` for safety, but do not expect savings.

**LTO** — `-Oz -flto` produced a **4144 b** object versus 937 b without. At the object
stage LTO embeds bitcode, which is far larger than machine code. It may pay off across a
whole link, but it is not an object-level size win and must be measured per-build, not
assumed.

**`-g0`** — 937 b with and without. clang does not emit DWARF by default here; the
`.debug$S` section is COFF symbol info that comes from `-gcodeview`-style flags, not `-g`.

## 4. Rules that follow

**§S1. Build with `-Oz`.** Not `-O2`. Measured 2.6× difference on real code.

**§S2. `-fno-exceptions -fno-rtti` is mandatory anyway** (OS-CODING-RULES §5). No
measurable object-level difference was observed on the test file — the cost shows up at
link time as unwind tables and RTTI descriptors, which is why the *whole-program* size
must be measured, not per-object.

**§S3. Put cold code in discardable sections.** Boot-only and error-reporting paths go in
`INIT`; they are freed after boot. This is the largest available structural win in a
kernel and costs nothing at runtime.

**§S4. No size-driven cleverness.** No macro-generated code to save bytes, no
hand-rolled varargs, no token pasting — banned by coding rule §17 regardless of size
benefit. If size requires a trick that a reviewer cannot read, choose the larger readable
version and record why.

**§S5. Measure the linked artifact, never the object.** §3 shows several flags that
shrink or grow objects while leaving the binary unchanged, and vice versa. The gate is
`stat -c%s` on the final PE.

**§S6. Size regressions are a test.** Add the linked size of each replacement subsystem
to the conformance runner's output, with a budget per subsystem. A change that grows a
subsystem beyond budget fails review the same way a failing test does.

## 5. Budgets

Set once P1 lands and there is something to measure. Provisional ceilings, to be revised
against real numbers rather than aspirations:

| Artifact | Budget | Note |
|---|---|---|
| Any replacement subsystem | ≤ 64 KB of `.text` | DNS codec, crypto provider, DHCP client |
| Any single translation unit | ≤ 8 KB of `.text` | a module bigger than this is doing too much |
| Any function | ≤ 60 lines (coding rule §14) | correlates with small code |

These are ceilings, not targets. Being under is fine; the point is that growth is a
decision rather than an accident.

## 6. Reproducing these numbers

```bash
export PATH=$HOME/.local/opt/llvm-mingw-*/bin:$PATH

# object-level comparison
for f in -O2 -Os -Oz; do
  clang $f -ffreestanding -fno-builtin --target=x86_64-windows-msvc -c mod.c -o m.obj
  echo "$f $(stat -c%s m.obj)"
done

# where the bytes are in a real PE
objdump -h ~/anemos-private/dist/kernel/ntoskrnl.exe
```

The `objdump -h` section table is the authority for "did this shrink", because it excludes
file-alignment padding that can mask small changes (a 200-byte change in a 1024-byte
minimum-alignment PE shows as no change at all — see §3).
