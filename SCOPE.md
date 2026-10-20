# Scope of this project's own work

MIT-licensed (see LICENSE). Everything listed here was written for this project.

## Licensed under MIT — original work

**Anemos replacements (`anemos/`)**
- `anemos/md5/`, `sha1/`, `base64/`, `crc32/`, `nls/`, `statusdos/` — P1
- `anemos/dns/`, `dhcp/` — P2 codecs and clients
- `anemos/crypto/`, `csp/` — P3 engine proof and CSP SPI skeleton
- their tests, independent Python reference implementations, corpora and gates

**Build harness and tooling**
- `harness/razzle.sh`, `harness/razzle-env.cmd`
- `harness/razzle-amd64.sh`, `harness/razzle-amd64-env.cmd`
- `harness/xpbuild.py`, `harness/bin/mc.cmd`, `harness/conformance/runner.py`
- `tools/abi/*`, `tools/build_codemap.py`, `tools/gen_amd64_status_inc.py`,
  `tools/ntfspatch.py`, `tools/toolchain.lock`

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
- all `docs/*.md`
- `docs/m0/` (except `EvcrtMsg.h.generated`, generated from Microsoft's message file)
- `README.md`, `PHASES.md`, `STATE.md`, `MODERNIZATION_PLAN.md`,
  `STANDING_INSTRUCTIONS.md`
- `docs/OPEN-REPLACEMENT-PLAN.md`, `docs/TASK-REQUIREMENTS.md`

## NOT licensed by us — third-party material

**`Source/` — 277,866 files, outside this repository**

This is leaked Microsoft Windows XP source code. It is **not** part of this
repository or its history. The `Source/` entry here is a gitignored symlink to
a local copy (by default `/home/marocos/winxp_source/Source`), kept as a
**reference and test oracle only**. It is:

- not owned by this project
- not licensed by this project, under MIT or anything else
- not distributed by this project
- not a contribution to it

Nothing in `LICENSE` grants any right to it. Reading it, building it, or comparing
against it confers no license. A clone or fork of this repository contains no
`Source/` symlink and no XP source; never add the tree to the repository or a
release.

## NOT licensed by us — built from Microsoft's source

These are compiled or generated artifacts. The build is ours; the code inside
is Microsoft's.

- `dist/kernel/ntoskrnl.exe`, `hal.dll`, `bootvid.dll`, `kdcom.dll`
- `docs/kernel-build/ntoskrnl-built.exe`, `ntoskrnl-shipped.exe`
- `docs/razzle-evidence/**/*.lib` and `*.log`
- `docs/codemap/` — generated map of Microsoft's source tree
- `tags` — ctags index of Microsoft's source tree
- `docs/m0/EvcrtMsg.h.generated`
- anything produced under `Source/XPSP1/binaries.*/`

They are **not** part of this repository or its history. Local copies are kept
outside it in `~/anemos-private/` for verification and local tooling. They are
**not** MIT and must not be redistributed as part of a licensed release.

## Not source at all

- `*.pyc`, `boot/out/`, `kernel/out/` — generated, safe to delete
- `tags` — kept in `~/anemos-private/`, gitignored here

---

If this scope and the repository contents ever disagree, the repository is wrong.
`git ls-files` is the authority.
