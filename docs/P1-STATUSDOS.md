# P1 STATUS→DOS: a replacement verified against XP for all 2^32 statuses

`anem_status_to_dos_error(int32_t status) -> uint32_t`, in
`anemos/statusdos/`, is Anemos's replacement for XP's
`ntdll!RtlNtStatusToDosError`. It is **done and closed by evidence**: a
freestanding probe called XP's own export for every one of the 2^32 possible
NTSTATUS values and compared each answer with this model. The in-guest
certificate says

```
VERIFIED: the model matched XP for all 4294967296 statuses (832 table entries, crc32 35550bd0)
```

and that certificate is committed at `anemos/statusdos/cert/sdos-cert.bin`
(72 bytes: header + footer, zero mismatch records). `anemos/statusdos/run.sh`
is 14/14 with it, including the negative controls.

There is **no Microsoft source in this directory**. Microsoft's `error.c` was
read only to know which order to *test* the rules in; every mapping in the
table is an answer observed from XP's behaviour, and the arithmetic that turns
one into the other is Anemos's own.

## The model

Six rules, applied in XP's order (`statusdos.c`), then an exact-match table:

| # | Condition | Answer | Evidence |
|---|---|---|---|
| 1 | `status & 0x20000000` | the status itself | walk: 426,590 records, incl. all of `0x60000000+` |
| 2 | `(status & 0xffff0000) == 0x80070000` | `status & 0xffff` | walk: 65,535 records where XP == low 16 bits |
| 3 | `(status & 0xf0000000) == 0xd0000000` | clear `0x30000000`, keep going | walk: 741 `0xD…` records matching their `0xC…` twins |
| 4 | exact table hit | the table's DOS error | walk: 832 entries |
| 5 | `(status >> 16) == 0xC001` | `status & 0xffff` | redirector-mangled OS/2 codes |
| 6 | otherwise | 317 (`ERROR_MR_MID_NOT_FOUND`) | walk: everything else, incl. 0xC000013A |

Rule 1 is a **bit test, not a range**. That is the single most important fact
learned here: the first model used `[0x20000000, 0x40000000)` and would have
been wrong for 1.5 billion statuses. The proof is quadrant 2 of
`mv-20261020-001903`: 262,135 statuses of the form `0x60000000+` where XP
returns the status itself.

## The exception table

`model.txt` is the source of truth and holds only what the rules do not already
produce; `gen_model.py` turns it into `table.c` and is the only thing allowed to
write that file, so the compiled table cannot drift from the model the tests
teach. The generator refuses, with a reason:

- a duplicate or out-of-order status;
- an entry whose value a rule already produces (redundant — it could never
  change a lookup, so it is a modelling mistake);
- an entry a rule intercepts before the table is read (unreachable: customer
  bit, `0x8007…`, or the `0xD…` form rule 3 rewrites);
- any of the published anchors resolving to the wrong value.

832 entries, 6,656 bytes of `.rodata`, `crc32 0x35550bd0`.

## How it is verified

Data flows the useful direction now: **the model is the hypothesis, XP is the
oracle, and only the disagreements come back.**

1. `mv_probe.c` — freestanding i686 PE (10,240 B, imports NTDLL+KERNEL32 only,
   subsystem console 5.01; 6.0 does not load on XP). It embeds the model,
   iterates all 2^32 statuses, calls the shipped `RtlNtStatusToDosError` for
   each, and writes back **only mismatches** plus counts. Output is KB, not
   gigabytes. Per-quadrant cap `0x40000` records so a broken model cannot fill
   the guest's disk; a heartbeat line every 2^28 statuses (16 lines) shows the
   pace on screen.
2. The certificate: header `SDMV` + version + table entry count + table CRC32
   (16 B), 12-byte `(status, model answer, XP answer)` records ascending, footer
   `SDMX` + total + per-quadrant counts + cap mask + `statuses checked` (56 B).
   The header CRC is what ties a certificate to one exact table: a certificate
   for another table is refused.
3. `check_cert.py` — VERIFIED only if the footer says all 2^32 statuses were
   checked, the count agrees with the records, records are ascending, every
   record reproduces the current model's answer, and no refusals fire. A
   truncated walk (no footer) is DIVERGENT, never VERIFIED.
4. `learn.py` — folds a DIVERGENT certificate into `model.txt`, dry-run by
   default. It refuses rule-shaped divergence (that would mean the *rules* are
   wrong, not the table), keys corrections on the unwrapped status for rule 3,
   and drops corrections a rule already produces.
5. `selftest.sh` — the loop itself, without a VM: `selftest_oracle.c` stands in
   for XP and diverges at five planted statuses, so the run must go
   walk → DIVERGENT → learn → walk → VERIFIED, and both cross-model refusals
   must fire. 18/18, including "the real `model.txt` was never modified".
6. `mv_run.sh` — the guest half: swap the probe into a victim file
   (`\windows\system32\notepad.exe`, 69,632 B allocation), boot, launch through
   the Run box, poll pixels until the console closes, shut down, extract
   host-side with `tools/ntfspatch.py`, check the certificate, restore the
   victim byte-identically. `--check` re-checks a certificate; `--restore` puts
   the victim back by hand.

Convergence took three walks: 1,559 learned mismatches (`mv-20261020-001903`:
quadrant 0 = 0, 1 = 9, 2 = 70, 3 = 1,480), then zero
(`mv-20261020-022220`). A walk is ~1 h 45 min on a single-vCPU XP guest;
`run.sh` never boots anything.

## Three things the walk proved that documentation gets wrong

The project rule is that XP wins. These were not "fixed" in XP's favour by
argument; each was decided by the walk and is recorded in `model.txt`.

1. **The customer rule is a bit test.** `0x60000000+` is identity, not 317.
2. **`STATUS_OBJECT_PATH_NOT_FOUND` (0xC000003A) → 3**, not 2. The first anchor
   list had `ERROR_FILE_NOT_FOUND` there; that belongs to
   `STATUS_OBJECT_NAME_NOT_FOUND` (0xC0000034).
3. **`STATUS_CONTROL_C_EXIT` (0xC000013A) is not in XP's table at all** — XP
   returns 317 (`ERROR_MR_MID_NOT_FOUND`), not the documented 1223. The entry
   was deleted rather than kept.

A fourth artefact is worth naming because it looked like data: the old raw
dump's very last triple was `0x2028669D -> 1`, sitting exactly at that dump's
corruption boundary. The walk asked XP again and got `0x2028669D` — identity,
as the customer rule says. The dump was damaged at its tail; the entry was an
artefact. `verify_prefix.c` now drops that single record, and only when it is
exactly that status with exactly that value, so any other disagreement still
fails.

## Why the raw-dump approach was abandoned (kept for the record)

The first attempt had the probe dump XP's whole mapping as run-length triples
(`sdos_probe.c`, `gen_table.py`, `verify.c` — since deleted).

- **Run-cap arithmetic.** The customer region is near-injective (≈1 run per
  status, 33.5M distinct DOS values in the first 13% of the space), so it needs
  >6 GB of triples and a full dump needs tens of GB. Worse, the cap expires
  inside that region, so a raw dump **never reaches `0xC0000000+`** — never
  reaches the error region where every anchor lives.
- **Guest instability under sustained write load.** One run bugchecked
  (`c000021a`, param `c0000013`); in another, the file beyond offset 31,772,676
  was written scrambled — structure invalid, but disk bytes matching the read
  bytes, and runlist sums exact, so not the extractor and not bit rot. The same
  binary tiles perfectly under Wine.

One genuine dump survives as a cross-check: `sdos-ext2`'s byte-perfect prefix
(statuses `0x0`–`0x2028669D`, 2,647,723 runs, 381 MB, untracked at
`harness/conformance/out/sdos-ext2/guest-sdos.bin`). `run.sh` step 4 walks
every status in it natively against the shipped table and step 5 proves the
walk fails under an off-by-one binary search.

## Reproducing

```sh
anemos/statusdos/run.sh          # host suite: 14/14, no VM, ~3 min
anemos/statusdos/selftest.sh     # the verification loop with a synthetic XP, ~1 min
anemos/statusdos/mv_run.sh --install   # one real walk against xp.vmdk (~2 h)
anemos/statusdos/mv_run.sh --check cert/sdos-cert.bin
```

`mv_run.sh` needs `xp.vmdk` to be the subject disk and no other VM running. Two
guest behaviours cost real time and are worth knowing:

- **The XP screensaver defeats pixel-based completion detection** (`logon.scr`
  is a black screen with a logo, nearly as dark as an unstyled console). The
  runner now sends a Shift keypress before each poll; a console app that never
  reads stdin ignores it.
- **A "Found New Hardware" wizard can block an otherwise clean shutdown.** It
  is harmless — the probe closes and flushes its certificate before exiting, so
  a hard power cut afterwards is safe — but it can cost the shutdown grace.
  Send the probe's exit as the signal, not the power state.

The probe build recipe (`run.sh` step 7 checks both facts):

```sh
clang --target=i686-w64-mingw32 -nostdlib -ffreestanding -fno-builtin \
  -fno-stack-protector -Oz -Wall -Wextra -Werror \
  -o out/mv_probe.exe mv_probe.c statusdos.c out/table.c \
  -Wl,-e,_mainCRTStartup -Xlinker /subsystem:console,5.01 -lntdll -lkernel32 -s
```

## Limits

- The probe carries the table, so it grows with it: 10,240 B now against a
  69,632 B victim allocation. `run.sh` step 7 fails if it stops fitting, which
  is the point — the ceiling is the guest's, not ours.
- The mapping is verified for the XP SP1 build on `xp.vmdk`. A different build
  is a different oracle: re-run the walk and it either still converges or tells
  you exactly where that build differs.
- DOS errors are 16-bit, and the model returns `ULONG` like XP's export —
  callers that truncate get XP's behaviour, including the identity cases.
