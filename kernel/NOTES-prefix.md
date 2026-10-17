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
