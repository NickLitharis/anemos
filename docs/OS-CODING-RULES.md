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

**Toolchain is pinned:** clang 23.1.3 + lld-link 23.1.3 (latest upstream release).
See `docs/TOOLCHAIN-POLICY.md` and `tools/toolchain.lock`. Nothing in the deliverable
is built by the 2001-era MSVC in the tree; that toolchain remains only for building
the legacy kernel used as the oracle.


**Size note:** build with `-Oz`, not `-O2` — measured 2.6x smaller on real code. See
`docs/SIZE-POLICY.md` for the full set of measured size levers, the flags that turned
out not to help, and the per-subsystem budgets.


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

## 13. Every loop has a provable bound

**Rule.** Every loop has a statically provable upper bound on its iterations. Loops that
are meant not to terminate state that explicitly and are confined to the scheduler and
dispatcher, where the reverse property is proven: they cannot exit.

**Failure mode.** An unbounded traversal of a corrupt linked list hangs the machine with
no bugcheck, no dump, and no way to distinguish it from a hardware fault.

```c
// NO — corrupt list means hang
while (p != NULL) { p = p->Next; }

// YES — bound + assert
ULONG guard = 0;
while (p != NULL) {
    ASSERT(++guard <= ANEM_MAX_LIST_DEPTH);
    if (guard > ANEM_MAX_LIST_DEPTH) return STATUS_DATATYPE_MISALIGNMENT;
    p = p->Next;
}
```

Source: NASA/JPL Rule 2.

## 14. Functions stay small

**Rule.** No function exceeds ~60 lines of statements. If it does not fit on one screen,
it is doing more than one thing.

**Failure mode.** Long functions are where resource leaks live: the cleanup path is
buried hundreds of lines from the acquisition, and no reviewer traces it.

Source: NASA/JPL Rule 4. NT's own code violates this constantly — `NtCreateFile` in
`io/file.c` runs to thousands of lines — which is exactly why we do not copy its shape.

## 15. Assertion density: two per function, side-effect free

**Rule.** Average at least two assertions per function. Assertions are Boolean,
side-effect free, and on failure the function returns an error to its caller rather than
continuing.

**Side-effect free is not optional.** An assertion that changes state is disabled in free
builds, which silently changes behaviour between checked and free kernels — the worst
class of bug, because it only appears in the configuration you did not test.

Verified good news: XP's own `ASSERT` is already side-effect free —
`((!(exp)) ? (RtlAssert(...),FALSE) : TRUE)` in `public/sdk/inc/wdm.h`, and
`((void) 0)` in free builds. We inherit a correct macro.

Source: NASA/JPL Rule 5.

## 16. Smallest possible scope

**Rule.** Declare data at the smallest scope that works. No file-scope variable that could
be a local. No reusable "scratch" variable for two unrelated purposes.

**Failure mode.** Wide scope means the value can be corrupted from anywhere, and the
assignment that broke it could be a thousand lines away.

Source: NASA/JPL Rule 6.

## 17. No setjmp/longjmp. No recursion. `goto` only for forward cleanup.

**Rule.**
- `setjmp`/`longjmp`: **absolutely banned.** They jump across frames with no unwind,
  skipping every lock release and pool free on the way.
- Recursion: **banned** in kernel mode. An acyclic call graph is what makes stack depth
  statically provable. Where recursion looks natural (tree walks, splay operations),
  use an explicit stack with a bound.
- `goto`: permitted **only** as a forward jump to a cleanup label within the same
  function, the idiomatic single-exit error path. Backward `goto` is banned.

**Why `goto` is narrowed rather than banned:** NASA Rule 1 forbids it outright, but NT's
error-handling style depends on it, and a chained `if (fail) { unlock(); free(); return; }`
repeated six times is *more* dangerous than one cleanup label, because a reviewer cannot
see that all six match. We take the stricter-than-NASA position on recursion and setjmp
and the pragmatic one on forward cleanup.

Source: NASA/JPL Rule 1, narrowed.


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
| `setjmp` / `longjmp` | jumps frames with no unwind — skips every lock release and pool free (§17) |
| Recursion | makes stack depth unprovable (§17) |
| Loop over a list with no iteration cap | corrupt list hangs the machine silently (§13) |
| Side-effecting assertion | disabled in free builds, so behaviour differs between checked and free (§15) |
| Backward `goto` | forward-to-cleanup only (§17) |

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

---

## Appendix — NASA/JPL "Power of Ten" (Holzmann, 2006)

Source: *The Power of Ten — Rules for Developing Safety Critical Code*, Gerard J. Holzmann,
NASA/JPL Laboratory for Reliable Software. Retrieved from `spinroot.com/gerard/pdf/P10.pdf`.
Copy at `docs/reference/P10-nasa-jpl.pdf`, extracted text at `docs/reference/P10-nasa-jpl.txt`.

The paper's own thesis is the reason to cite it: a coding standard is only useful if it
**can be checked mechanically**, and ten rules that are enforced beat a hundred that are
not. That is the design principle behind our own seventeen rules.

### Adopted (7 of 10)

| # | Rule | How it lands here |
|---|---|---|
| 2 | All loops have a statically provable bound | §13 of our rules. The paper exempts scheduler/dispatcher loops by requiring the *reverse* proof (cannot terminate) — applies directly to our dispatcher. |
| 4 | No function longer than ~60 lines | §14 (new). NT's own code routinely exceeds 1000. |
| 5 | ≥2 assertions per function, side-effect free, Boolean, with explicit recovery | §15 (new). Note the paper's insistence on **side-effect-free**: checked below, XP's `ASSERT` complies. |
| 6 | Declare objects at the smallest possible scope | §16 (new). |
| 7 | Every return value checked; parameters validated in callee | Already §4. Paper adds the useful refinement that an explicit `(void)` cast is the sanctioned way to say "deliberately ignored", rather than silence. |
| 10 | Pedantic warnings, zero warnings, static analysis daily | Already §10. Strongest independent corroboration in the document. |
| 1 | No `setjmp`/`longjmp`, no recursion | §17 (new). Adopted for recursion and setjmp; **`goto` narrowed, not banned** — see below. |

### Rejected (3 of 10) — with reasons

**Rule 3 — "No dynamic memory allocation after initialization."**
Inverts for a kernel: a kernel *is* the allocator. The rule exists so an application's
worst-case memory is statically provable; a kernel cannot have that property. Restated
as our §1: allocation is allowed but must go through a tagged pool with failure handled
at the call site.

**Rule 8 — "Preprocessor limited to includes and simple macros; no token pasting, no
varargs, no recursion; minimal conditional compilation."**
Would forbid the entire NT SDK. `CONTAINING_RECORD`, `FIELD_OFFSET`, `NT_SUCCESS`,
`RtlOffsetToPointer` are all token-pasting macros, and the tree uses conditional
compilation per-architecture pervasively. Adopting this literally means writing an OS
with no headers. We keep the *spirit*: no clever macros in new code, and no macro that
hides control flow.

**Rule 9 — "No more than one level of dereference; function pointers not permitted."**
Fatal to an operating system. Function pointers *are* the architecture: driver dispatch
tables (`DRIVER_OBJECT.MajorFunction`), IRP dispatch, the I/O manager, callbacks. The
paper acknowledges the trade-off ("should only be used if there is a strong
justification") — that justification is "this is an OS". We adopt the single-dereference
guidance as a default and permit dispatch tables with a comment.

### A claim I checked and had to withdraw

An earlier draft of this appendix said XP's `ASSERT` releases locks and is therefore an
unsafe side-effecting assert. That is **false**. Verified in
`public/sdk/inc/wdm.h`:

```c
#define ASSERT( exp ) \
    ((!(exp)) ? \
        (RtlAssert( #exp, __FILE__, __LINE__, NULL ),FALSE) : \
        TRUE)
```

and in checked-out builds `#define ASSERT( exp ) ((void) 0)`. No locks, no side effects.
XP's ASSERT already satisfies NASA Rule 5's side-effect-free requirement. Recorded here
because a wrong rationale in a coding standard is worse than no rationale.

### Where NASA and this project genuinely disagree

NASA's rules target **flight software**: single-purpose, statically analyzable, finite
lifetime, no dynamic anything. An OS kernel is the opposite of all four. Seven of ten
rules transfer cleanly; the three that don't are exactly the three about eliminating
dynamism, and eliminating dynamism is what a kernel does.
