# P1 STATUS→DOS: extraction attempt — findings and blocked status

`anem_status_to_dos_error(int32_t status) -> uint32_t` (P1 item 6) needs XP's
`ntdll!RtlNtStatusToDosError` behaviour for all 2^32 statuses. This document records
the clean-room extraction attempt of 2026-10-19: what was proven, why the raw-dump
approach is structurally infeasible, and what the rework must be.

## What was built and verified

- **Probe** `anemos/statusdos/sdos_probe.c`: freestanding i686 PE
  (`-nostdlib -ffreestanding -fno-builtin`, entry `mainCRTStartup`, **subsystem
  console 5.01** — the 6.0 default does not load on XP; Wine ignores this, XP
  refuses it), imports NTDLL+KERNEL32 only, 3,072 B. Iterates all 2^32 statuses,
  RLE-encodes `(first, count, dos)` 12-byte LE triples, flushes every 5,461 runs
  via `WriteFile` with `FILE_FLAG_WRITE_THROUGH` + final `FlushFileBuffers`,
  caps output at 2^25 triples (~381 MB) so a fragmented mapping cannot fill the
  guest's C:.
- **Toolchain** (`gen_table.py`, `verify.c`, `test_statusdos.c`, `run.sh`): the
  table generator refuses any dump that does not tile [0, 2^32) exactly or whose
  content disagrees with 9 published anchors; the run suite (Wine + native
  exhaustive walk over 2^32 against the raw dump + negative controls + freestanding
  i686-windows-msvc compile) passes **6/6 on a synthetic reference dump**, with both
  negative controls failing as required.
- **Runner** `harness/conformance/runner.py` gained an `extract` mode (committed):
  launches the probe through the **Win+R Run box** — shell-owned and modal, immune to
  the focus thief that silently ate the console-channel probe line twice — verifies
  launch *and* completion in pixels (black probe console vs. Bliss wallpaper), fixes
  two real bugs (`vncdo(["capture",…])` crash; `rstrip(b"\x00")` that would have
  corrupted binary dumps).
- **Clean-room extraction data**: one genuine XP run (tag `sdos-ext2`) produced a
  **byte-perfect, tiling-perfect prefix: statuses 0x00000000–0x2028669D, 2,647,723
  runs** (re-extracted twice, runlist math verified exactly against the MFT).
  Its content matches published XP behaviour: `0→0`, unknown low codes
  `1..0x102→317` (`ERROR_MR_MID_NOT_FOUND`), consistent with table+default. Kept at
  `harness/conformance/out/sdos-ext2/guest-sdos.bin` (untracked, 381 MB).

## Why the raw-dump approach is dead (independent of guest health)

1. **Run-cap arithmetic.** The tail of a cap-hit dump is the customer region
   (`STATUS_CUSTOMER_BIT`, 0x20000000–0x3FFFFFFF): near-injective, ≈1 run per
   status (33.5M distinct DOS values across the first 13% of the space — measured
   on both Wine and XP). Capturing that region alone as triples needs **>6 GB**;
   the full mapping needs tens of GB. Worse, the cap expires inside the customer
   region — **a raw dump never reaches 0xC0000000+ at all**, i.e. never reaches
   the error-severity table where every published anchor lives.
2. **Guest instability under sustained write load** (separate finding): a second
   run (`sdos-repro1`) **bugchecked** — BSOD `c000021a`
   (`STATUS_SESSION_PROCESS_TERMINATION`, param `c0000013`) — shortly after probe
   launch; in `sdos-ext2` the file's tail beyond offset 31,772,676 was written
   scrambled (structure invalid, not bit rot; not the extractor: disk bytes match
   the read bytes, runlist sums exactly). The probe binary is exonerated: the same
   binary produces a perfectly tiling dump under Wine.

## What the rework must be

Invert the data flow: **model-verification probe**. The probe embeds a hypothesis
(rules + compact table, generated host-side), iterates 2^32 in-guest, and writes
back only mismatches (capped) plus a total count — output in KB, minutes of
write load instead of gigabytes. The customer region's rule (≈`dos = status`,
30M consistent tail samples) and the `default → 317` rule are already evidenced;
the 0xC0000000+ table entries must be learned iteratively from the mismatch
stream. Convergence is a few VM runs on a guest that must first be shown stable
under the (now tiny) workload.

## Probe reinstall recipe (subject disk; original backed up host-side)

```sh
clang --target=i686-w64-mingw32 -nostdlib -ffreestanding -fno-builtin \
  -fno-stack-protector -Oz -Wall -Wextra -Werror \
  -o anemos/statusdos/out/sdos_probe.exe anemos/statusdos/sdos_probe.c \
  -Wl,-e,_mainCRTStartup -Xlinker /subsystem:console,5.01 -lntdll -lkernel32 -s
python3 tools/ntfspatch.py ~/vms/xpvmware/xp.vmdk put \
  '\windows\system32\write.exe' anemos/statusdos/out/sdos_probe.exe   # swap in
python3 tools/ntfspatch.py ~/vms/xpvmware/xp.vmdk put \
  '\windows\system32\write.exe' /tmp/opencode/p1/write-orig.exe       # restore
python3 harness/conformance/runner.py extract --disk ~/vms/xpvmware/xp.vmdk \
  --tag <fresh-tag> [--settle N]
```

`write.exe` on the subject disk was **restored byte-identical** after the attempt.
