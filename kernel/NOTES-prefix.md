# prefix.c status: compiles, second insert hangs (open)

`base/ntos/rtl/prefix.c` compiles cleanly (16 routines) and is linked into
`libxprtl.a`, but the in-guest test **hangs on the second insert** (inserting a
longer prefix "ABC" into a table already holding "AB").

## What was established

- The first insert into an empty table works and returns TRUE.
- `RtlInitializeUnicodePrefix` makes the table its own sentinel
  (`NextPrefixTree = (PUNICODE_PREFIX_TABLE_ENTRY)PrefixTable`), so the walk
  `while (CurrentTree->NameLength > PrefixNameLength)` terminates on the
  aliased `NameLength` (0) of the table header.
- `UNICODE_PREFIX_TABLE` and `UNICODE_PREFIX_TABLE_ENTRY` layouts were verified
  against the tree's `ntrtl.h`; the first four fields share offsets, so the
  sentinel cast is sound.
- `RtlSplay`, `RtlDelete`, `RtlSubtreePredecessor/Successor` all link.
- A real bug was fixed on the way: `CSHORT` had been declared as `long` in the
  test harness; the tree uses `typedef short CSHORT` (16-bit). That changed the
  failure from a hard fault to a hang.

## Not established

Where the loop actually spins. Finding it needs stepping or in-guest print
instrumentation inside the XP function, which the current harness does not
provide (tests can only print between calls).

## Decision

`prefix.c` stays linked but **unverified**. The verified set remains 8 files.
Do not claim prefix support until this test passes.

## Next step when picked up

Add a tiny in-guest tracing shim (a hook the XP sources call, e.g. `DbgPrint`
stub writing to port 0xE9) so the loop can be observed without a debugger.
That shim will be useful for every future kernel file, not just this one.

## Tracing results (DbgPrint shim added since)

An in-guest `DbgPrint` shim (writes to port 0xE9) was added to
`kernel/inc/xpkernel.h` and used to instrument `prefix.c` in a scratch copy:

```
about AB
insert AB=1 OK
about ABC
INS want=1
ITER node=26C0 prefix=267C      <- loop entered once, both pointers sane
<hang, CMPR never printed>
```

Established:

- `UNICODE_PREFIX_TABLE_ENTRY` is 28 bytes with `CaseMatch@8`, `Next@4`,
  `NameLength@2`; `UNICODE_PREFIX_TABLE` is 12 bytes. Both match the tree.
- `ComputeUnicodeNameLength` returns 1 for both "AB" and "ABC" (it counts
  backslashes), so the second insert takes the tree-descent path, not a new
  length bucket.
- `Node` (0x26C0) and `Node->Prefix` (0x267C) are valid pointers when the loop
  is entered.
- The hang occurs after the loop top and before the `Comparison` line prints,
  i.e. inside or entering `CompareUnicodeStrings(Node->Prefix, Prefix, 0)`.

So the remaining suspect is the compare's own char loop, not the tree. That
needs either a watchpoint or a print inside the `for` loop, which requires
instrumenting a *nested* body — the next concrete step.

## Decision (unchanged)

prefix.c stays linked and unverified. Verified set remains 8 files.
