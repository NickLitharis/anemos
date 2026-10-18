# Scope of this project's own work

MIT-licensed (see LICENSE). Everything listed here was written for this project.

## Licensed under MIT — original work

**Build harness and tooling**
- `harness/razzle.sh`, `harness/razzle-env.cmd`
- `harness/razzle-amd64.sh`, `harness/razzle-amd64-env.cmd`
- `harness/xpbuild.py`, `harness/bin/mc.cmd`
- `tools/build_codemap.py`, `tools/gen_amd64_status_inc.py`, `tools/ntfspatch.py`

**Kernel experiments (`kernel/`)**
- `kernel/build.sh`, `kernel/gen_runtable.py`
- `kernel/inc/xpntdef.h`, `kernel/inc/xpkernel.h`
- `kernel/src/*.c` — the freestanding shims, arena, and generated tables
- `kernel/tests/*.c` — all test suites
- `kernel/rtl_string_standalone.c`, `kernel/*.md`

**Boot experiments (`boot/`)**
- `boot/build.sh`, `boot/link.ld`, `boot/run_all.sh`
- `boot/*.c`, `boot/rtl/*.c`, `boot/tests/*.c`
- `boot/README.md`

**Documentation (`docs/`, root)**
- all `docs/*.md`, `docs/codemap/*` (generated index of the tree)
- `docs/m0/`, `docs/kernel-build/*.log`
- `README.md`, `STATE.md`, `MODERNIZATION_PLAN.md`, `STANDING_INSTRUCTIONS.md`
- `docs/OPEN-REPLACEMENT-PLAN.md`, `docs/TASK-REQUIREMENTS.md`

## NOT licensed by us — third-party material

**`Source/` — 277,866 files**

This is leaked Microsoft Windows XP source code. It is present in this working copy
as a **reference and test oracle only**. It is:

- not owned by this project
- not licensed by this project, under MIT or anything else
- not distributed by this project
- not a contribution to it

Nothing in `LICENSE` grants any right to it. Reading it, building it, or comparing
against it confers no license. If you clone or fork this work, **exclude `Source/`.**

## NOT licensed by us — built from Microsoft's source

These are compiled artifacts. The build is ours; the code inside is Microsoft's.

- `dist/kernel/ntoskrnl.exe`, `hal.dll`, `bootvid.dll`, `kdcom.dll`
- `docs/kernel-build/ntoskrnl-built.exe`, `ntoskrnl-shipped.exe`
- `docs/razzle-evidence/**/*.lib` and `*.log`
- anything produced under `Source/XPSP1/binaries.*/`

They exist so the boot result can be independently verified. They are **not** MIT
and must not be redistributed as part of a licensed release.

## Not source at all

- `tags`, `*.pyc`, `boot/out/`, `kernel/out/` — generated, safe to delete

---

If this scope and the repository contents ever disagree, the repository is wrong.
`git ls-files` is the authority.
