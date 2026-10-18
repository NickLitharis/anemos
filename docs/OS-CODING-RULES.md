# OS coding rules

These are not style preferences. In ring 0 a wrong pointer is not an exception you
recover from — it is the machine. Each rule exists because breaking it has a specific,
catastrophic failure mode, named below.

Applies to all new Anemos code. Replacement subsystems follow these rules from the
first line; "we'll harden it later" is how kernels get CVEs.

**Language:** C23 for interfaces and most kernel code; C++23 (freestanding subset) where
RAII genuinely removes a bug class. See §9.

---

## 1. No hidden allocation

**Rule.** Kernel code never calls a general-purpose allocator. Allocation is explicit,
pool-tagged, and failure-checked at the call site.

**Failure mode.** A `std::vector` or `new` inside a spinlock takes the wrong IRQL path,
or blocks on a lazy commit while holding a lock, and the box deadlocks under load — not
in the test.

```c
// NO
void *p = malloc(n);              // wrong pool, wrong tag, unchecked
std::vector<uint8_t> buf(n);      // allocates, throws, both forbidden in ring 0

// YES
PVOID p = ExAllocatePool2(POOL_FLAG_NON_PAGED, n, 'svcA');
if (p == NULL) return STATUS_INSUFFICIENT_RESOURCES;
```

Note the pool tag `'svcA'` — four characters, reversed in dumps, so leaks are traceable
to a subsystem. Every allocation gets one.

## 2. IRQL is part of every function's contract

**Rule.** Any function that may run at `DISPATCH_LEVEL` or above documents it and touches
only non-paged memory. Anything that can block (waits, paged pool, mutexes) is documented
`PASSIVE_LEVEL` only.

**Failure mode.** Touching paged memory at `DISPATCH_LEVEL` is a `0x000000A` — and it
happens only when the memory happens to be trimmed out, so it looks like random
flakiness for months.

```c
// Every kernel entry point states its requirement:
// IRQL: <= DISPATCH_LEVEL. Must not block. Memory must be non-paged.
NTSTATUS AnemQueueWork(_In_ PWORK_ITEM Item);
```

## 3. Validate every buffer crossing a trust boundary

**Rule.** User pointers are never trusted. `ProbeForRead`/`ProbeForWrite`, then capture,
then use. Never probe-then-use without capturing — the user can change the buffer between.

**Failure mode.** Double-fetch. It is a local privilege escalation, and it is the single
most common Windows kernel vulnerability class.

```c
// NO — TOCTOU
ProbeForRead(User, sizeof(ULONG), 4);
ULONG n = *(ULONG *)User;          // attacker changed it after the probe
char *buf = ExAllocatePool2(..., n, 'x');

// YES — capture once
ULONG n;
if (!NT_SUCCESS(ProbeAndCapture(&n, User, sizeof(n)))) return STATUS_ACCESS_VIOLATION;
```

## 4. Every return value is checked, and errors are typed

**Rule.** `NTSTATUS` is checked with `NT_SUCCESS`. Errors propagate with meaning; no
"return 0 on failure" and no swallowing. `[[nodiscard]]` on everything that can fail.

**Failure mode.** An unchecked failure leaves a half-initialised structure that another
thread reads microseconds later, producing a corruption far from the cause.

## 5. No exceptions, no RTTI, no C++ runtime in ring 0

**Rule.** Freestanding subset only: `-fno-exceptions -fno-rtti -fno-threadsafe-statics`.
No `throw`, no `dynamic_cast`, no `typeid`, no global constructors with side effects.

**Evidence this is the right call:** win32k in this very tree ships **70 `.cpp` files and
zero uses of `try`/`catch`/`dynamic_cast`/`typeid`**. Microsoft themselves ran kernel C++
under exactly this restriction.

**Failure mode.** An exception unwinding through a kernel frame with no handler bugchecks
the machine, and static-constructor order is undefined across translation units.

## 6. Locking: declare the order, or you will deadlock

**Rule.** Any function taking more than one lock documents the acquisition order. Lock
order is global and consistent; violating it is a build error, not a review comment.

**Failure mode.** Classic ABBA deadlock, which only reproduces under concurrency and never
in a single-threaded test.

## 7. No uninitialised memory, ever

**Rule.** Structures are zeroed at allocation (`ExAllocatePool2` with `POOL_FLAG_ZERO`,
or explicit `RtlZeroMemory`). Padding is never compared (`memcmp` on structs with holes
compares garbage — and leaks it).

**Failure mode.** Uninitialised padding copied to user mode is an information disclosure.

## 8. Integer arithmetic is hostile input

**Rule.** All size arithmetic uses checked helpers (`RtlSizeTMult`, `RtlSizeTAdd`) at any
boundary. No raw `a * b` on values that came from outside.

**Failure mode.** Overflow defeats the allocation check in §3, and the kernel copies far
more than the buffer holds.

## 9. Language selection

| Layer | Language | Why |
|---|---|---|
| Public interfaces, exported ABI | **C** | Must stay binary-compatible for every existing caller |
| Kernel internals | **C23**, C++23 where RAII helps | no runtime, no exceptions either way |
| Replacement subsystems (DNS, crypto, DHCP) | **C23 or C++23** | fresh code with real invariants |
| User-mode tools | whatever is clearest | not shipped in ring 0 |

**Boundary discipline:** a C++ implementation exposes an `extern "C"` header and hides
everything else. That keeps the swap reversible — you can always put the original
implementation back and re-run the oracle.

## 10. Compiler settings are part of the standard

```
clang -std=c23 -ffreestanding -fno-builtin -fno-stack-protector
      -fno-exceptions -fno-rtti
      -Wall -Wextra -Werror -Wconversion -Wshadow -Wcast-align
      -Wstrict-prototypes -Wmissing-prototypes -Wnull-dereference
      --target=x86_64-windows-msvc
```

`-Wconversion` and `-Wcast-align` are non-negotiable: they catch the §8 and §3 bug
classes at compile time. Warnings are errors, so "it compiles" means something.

Static analysis with `clang-tidy` and `scan-build` gates the merge. ASan/UBSan run the
test suites; they do not run in the kernel.

## 11. Every function that can fail says how

**Rule.** No silent failure. Either return a status, or `ASSERT` a condition that must
hold by construction, or both. A function that can fail and returns `void` is a bug.

## 12. Tests before the replacement, not after

**Rule.** A subsystem is replaced only when a conformance test exists that the *current*
implementation passes and the *replacement* must also pass. Write the test against the
behaviour you can observe, then write the code.

**Why this is rule 12 and not rule 1:** the test is the only thing that distinguishes "I
rewrote it" from "I rewrote it correctly". Every replacement in this project is proven by
differential testing against XP, and without that harness the code is unverifiable.

---

## Anti-patterns explicitly banned

| Banned | Because |
|---|---|
| `malloc`/`free`/`new` in kernel code | §1 |
| `strcpy`, `sprintf`, `strcat`, unbounded `memcpy` | no bounds; use `RtlStringCbCopyA` family |
| `memcmp` on structs with padding | §7 |
| `goto` cleanup that skips a return-value check | silent leak |
| Magic numbers | use `FIELD_OFFSET`, `ARRAY_SIZE`, named constants |
| `#pragma warning(disable)` without a comment naming the reason | hides real defects |
| Casting away `const` | indicates a design error, not a need |
| Commented-out code | git remembers; dead code in a kernel is a liability |

---

## Sources

Not invented here. These rules reconcile:

- the constraints **demonstrated by this tree** (win32k's zero-exception C++, NT's
  pool-tagging discipline, `ProbeForRead`+capture patterns);
- Microsoft's published driver rules (pool tags, IRQL contracts, `__drv_` annotations);
- the vulnerability classes that historically hit Windows kernel code (double-fetch,
  integer overflow, uninitialised padding disclosure).

If a rule ever conflicts with observed XP behaviour, **XP wins** — it is the oracle, and
the goal is compatibility. Record the conflict and the reason.
