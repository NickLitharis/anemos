# G2 — the conformance runner

**Gate: PASSED 2026-10-18.** `harness/conformance/runner.py`, pointed at the two disks we
already have, reports **IDENTICAL** — for the same disk run twice (stability) and for
`xp-pristine.vmdk` vs `xp.vmdk` (the gate). Both ways it can report a difference were
demonstrated to report one.

`Source/` was not read, copied, or executed by anything in this document. The runner
treats the guest as a black box: it types, and it reads back what the guest wrote.

---

## What it does

```
runner.py run     --disk <vmdk> --tag <name>   boot, wait for desktop, probe, collect
runner.py compare <dirA> <dirB>                diff two collections; exit 1 on any diff
runner.py probe   --tag <name>                 re-drive the probe at an already-booted VM
```

One run: retarget `~/vms/xpvmware/xp.vmx` at the given disk (while powered off, always
restored in a `finally`), boot it, wait for the desktop, open `cmd.exe`, run the probe,
capture the screen, shut the guest down, extract the files the guest wrote host-side via
`tools/ntfspatch.py`. Everything lands in `harness/conformance/out/<tag>/`:

| Artifact | What it is |
|---|---|
| `meta.json` | boot outcome, seconds to desktop, console rect, sha256 of every guest file |
| `screen-focus-*.bmp` | proof that `cmd.exe` owned the keyboard (see below) |
| `screen-final.bmp` | the console after the probe — compared pixel-for-pixel |
| `screen-dir.bmp` | the guest's own `dir \conftest` — evidence, never compared |
| `guest-*.txt` | the six files the guest wrote, pulled out of the NTFS image |

**One VM, two disks.** The control run and the subject run must present the guest with the
same machine — same BIOS UUID, same MAC, same devices — so the kernel on the disk is the
only variable. A second VMX was tried and abandoned: a new UUID makes XP re-detect
hardware (Found New Hardware Wizard, 10-minute boots), and two concurrent VMs on this
15 GB host crawl. The runner refuses to start if any VM other than its own is running.

## The probe

Runs inside the guest, in `cmd.exe`, as one fixed list of lines (`PROBE_COMMANDS`):
`cd \`, `md conftest 2>nul`, `cd conftest`, then `ver`, `set`, `assoc`, `ftype`,
`dir /-c /b \windows\system32` and `driverquery`, each redirected into a file in
`\conftest`. Everything observed is the *output of the guest's own binaries*, which is
what makes a substitution visible later.

Two properties were not left to chance:

- **Keyboard focus is proven, not assumed.** After opening the console the probe types
  `color 4f`. If `cmd.exe` really owns the keyboard, its window turns red — and nothing
  else on a clean XP desktop is red (measured: `(128,0,0)`, not a bright red). No red
  after three attempts means the probe is *not* run and the run reports failure. A probe
  typed into the wrong window is a run that silently observed nothing.
- **The console region is derived from the pixels** (`red_bbox`), so the screen comparison
  covers exactly the window the probe owned. The taskbar clock, notification balloons and
  the mouse cursor live outside it and cannot make two runs differ.

The guest's own `dir \conftest` is captured to `screen-dir.bmp` but never compared: its
listing carries timestamps. It settles "did the files really exist" from a picture, which
is how the flush problem below was diagnosed.

## Measured evidence

Three runs, `xp-pristine.vmdk` twice and `xp.vmdk` once, 2026-10-18:

| Run | Disk | Desktop | Focus | Console pixels | Self power-off |
|---|---|---|---|---|---|
| `control1` | `xp-pristine.vmdk` | 26.3 s | attempt 1, red 0.3821 | `ab8d5fd1cf2c0c9b…` | yes |
| `control2` | `xp-pristine.vmdk` | 26.3 s | attempt 1, red 0.3821 | `ab8d5fd1cf2c0c9b…` | yes |
| `subject1` | `xp.vmdk` | 26.4 s | attempt 1, red 0.3821 | `ab8d5fd1cf2c0c9b…` | no (see below) |

Console rect identical in all three: `(18, 90, 690, 583)`.

Guest files, byte-identical across all three runs (sha256, sizes from the guest's `dir`):

| File | Size | sha256 |
|---|---|---|
| `ver.txt` | 43 | `0810e8c9d69d4557…` |
| `env.txt` | 917 | `22b644bf3bd4437d…` |
| `assoc.txt` | 4 189 | `899e21027dab9851…` |
| `ftype.txt` | 8 555 | `8dae573c53e50f86…` |
| `sys32.txt` | 21 996 | `3ea7f88dead9a5aa…` |
| `drivers.txt` | 7 106 | `a0cf7b1f9403a16d…` |

**Stability (A/A):** `compare control1 control2` → `IDENTICAL`, exit 0.
**The gate (A/B):** `compare control1 subject1` → `IDENTICAL`, exit 0. Both disks still
run Microsoft's kernel at this stage, so this is the expected result — it validates the
runner, not any replacement.

**Negative controls — both directions it must fail in, shown failing:**

```
# one byte appended to one extracted guest file
$ python3 harness/conformance/runner.py compare out/control1 out/neg-file
DIFFERENT (1):
  guest file ver.txt: content differs                          exit 1

# one pixel flipped inside the console region
$ python3 harness/conformance/runner.py compare out/control1 out/neg-screen
DIFFERENT (1):
  console window pixels: ab8d5fd1… != d1a6faf4…                exit 1
```

And one organic negative control, found before it was fixed: with a leftover `\conftest`
on the control disk, `md conftest` printed `A subdirectory or file conftest already exists.`
on one run and nothing on the other, and the comparison reported the console pixel
difference. The probe now writes `md conftest 2>nul`, because the observation must not
depend on what an earlier run left behind.

## The keyboard channel, measured

The guest is driven only by its keyboard (over VMware's VNC server, `vncdotool`). Every
claim below was measured against this VMware build, not assumed:

| Input | Measured |
|---|---|
| `type` of letters, digits, space, `.`, `-`, `/`, `\` | arrive intact |
| `type` of `>` | **arrives as `.`** — VMware maps that keysym to the unshifted key |
| `>` therefore | sent as `key shift-.` — produces a real `>` (`line_tokens()`) |
| `%` | never sent; `key shift-5` produces one if ever needed |
| `:` | documented dead key — never sent, so no probe line may contain one |
| uppercase | `key shift-x` (e.g. `type` of uppercase is avoided) |
| valid key names | vncdotool's `KEYMAP` only (`bsp`, `bslash`, `fslash`, `esc`, `super`, …) |
| invalid key name | **crashes vncdotool** (`ord()` on a multi-char name), so all names are from `KEYMAP` |
| opening the console | `key super-r` (Win+R) — Run box with a focused edit control |
| typing into the Start menu | **does not open Run** — XP uses typed characters for incremental icon selection (observed: Control Panel and Mouse Properties opened instead) |
| `ctrl-esc` | opens the Start menu; a typed letter there is not a command |
| bare pointer `move` | does not reposition the guest cursor — a following `click` lands at the stale position (opens the desktop context menu) |
| drag (`mousedown` → `move` → `mouseup`) | does reposition the cursor, but a drag started on the desktop never activates the Start button on release |
| ACPI soft stop from the host | never powered the guest off (grace expired every time) |

Consequences encoded in the runner: probe lines are split into `type` runs with `>` sent
as its own keystroke; the Run box is reached by Win+R, never by the Start menu; success is
measured in pixels; and the flush (below) does not depend on ACPI.

## The four failures this needed, and what they were

1. **Files existed in the guest and not on the disk.** The probe ran, the console showed
   every command succeed, and `ntfspatch` reported `not found`. The host's ACPI stop never
   powered the guest off, so the hard cut after the grace period destroyed an unflushed
   write cache — the directory survived, its contents did not (confirmed by searching the
   raw image for the filenames, and by the guest's own `dir` in `screen-dir.bmp`).
   **Fix:** the probe's last act is `shutdown -s` and the runner waits for the guest to
   power itself off. Only a real shutdown flushes; the hard cut is now a fallback that
   happens after the flush.
2. **`ntfspatch.listdir` read `$INDEX_ROOT` with the wrong offset.** It used the
   attribute-type field (`val[0:4]`, always `0x30`) where the node header's
   `OffsetToFirstEntry` sits at `0x10`. The root directory listed fine only because its
   entries spill into `$INDEX_ALLOCATION`; a small, freshly created directory keeps its
   entries in the resident index root and listed as empty — every probe file was
   "missing" while sitting on the disk. Fixed in `tools/ntfspatch.py`; extraction sizes
   now match the guest's `dir` exactly (43 / 917 / 21 996 bytes, measured both ways).
3. **The probe could not prove it had typed into `cmd.exe`.** Three focus attempts
   produced no red console because the keys had gone to the Start menu. The pixel proof
   (`color 4f` + red detection) was added so this fails loudly instead of silently.
4. **The screen comparison caught state, not behaviour.** Leftover `\conftest` changed
   one console line between runs. Fixed by `md conftest 2>nul`.

## Known observations

- **The subject disk does not power itself off after `shutdown -s`** (it stopped at
  ACPI/hard-cut in both subject runs while both control runs powered off cleanly). Its
  extracted files were still byte-identical, because the flush happens during shutdown,
  before the power-off. Not investigated yet: at this stage both disks run Microsoft's
  kernel, so this is a difference in the two *installs*, not in any replacement. It is
  recorded because once replacements start, a behavioural difference in shutdown is
  exactly the kind of signal this runner exists to surface.
- The console renders with a blank line between command lines on this VMware display.
  Deterministic (A/A compares byte-exactly); cosmetic; not investigated.

## Reproducing

Needs `vmrun` (VMware Workstation), `vncdotool` at `~/.local/bin/vncdo`, and the tree's
VM at `~/vms/xpvmware/xp.vmx` with VNC password `xpvm`. No root. One VM at a time — the
runner stops leftovers and refuses foreign ones.

```sh
P=~/vms/xpvmware
python3 harness/conformance/runner.py run --disk $P/xp-pristine.vmdk --tag control1
python3 harness/conformance/runner.py run --disk $P/xp-pristine.vmdk --tag control2
python3 harness/conformance/runner.py compare \
    harness/conformance/out/control1 harness/conformance/out/control2     # IDENTICAL
python3 harness/conformance/runner.py run --disk $P/xp.vmdk --tag subject1
python3 harness/conformance/runner.py compare \
    harness/conformance/out/control1 harness/conformance/out/subject1     # IDENTICAL
```

Each run is ~4 minutes (26 s to desktop, ~60 s probe, guest shutdown, extraction).
`harness/conformance/out/` is gitignored; `meta.json` in each run records what was
measured, not what was claimed.
