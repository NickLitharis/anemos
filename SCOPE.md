# Scope of this project's own work

MIT applies only to files with established original or third-party provenance, as listed below. Directory membership alone does not establish authorship or licensing.

## Licensed under MIT — original work

**Anemos replacements (`anemos/`)**
- `anemos/md5/`, `sha1/`, `base64/`, `crc32/`, `nls/`, `statusdos/` — P1
- `anemos/dns/`, `dhcp/` — P2 codecs and clients
- `anemos/crypto/`, `csp/` — P3 engine proof and CSP SPI skeleton
- their tests, independent Python reference implementations, corpora and gates

**AK1–AK3 kernel and boot**
- `kernel/main.c`, `kernel/linker.ld`, `kernel/serial.c`, `kernel/serial.h`,
  `kernel/interrupts.c`, `kernel/interrupts.h`, `kernel/interrupts.S`,
  `kernel/limine_requests.c`, `kernel/limine_requests.h`, `kernel/memory.c`,
  `kernel/memory.h`, `kernel/paging.c`, `kernel/paging.h`, `kernel/heap.c`,
  `kernel/heap.h`, `kernel/memory_test.c`, `kernel/memory_test.h`,
  `boot/limine.conf`, `boot/build.sh` — independently authored C23 kernel
  entry, Limine requests, serial diagnostics, x86_64 exceptions and timer,
  physical-page allocation, virtual mapping, bounded heap, and QEMU gate

The Limine v12.9.6 BIOS tools are downloaded to the user's cache by the build
script and checked against a pinned SHA-256. They are not checked into or
redistributed with this repository.

**Build harness and tooling**
- `harness/razzle.sh`, `harness/razzle-env.cmd`
- `harness/razzle-amd64.sh`, `harness/razzle-amd64-env.cmd`
- `harness/xpbuild.py`, `harness/bin/mc.cmd`, `harness/conformance/runner.py`
- `tools/abi/*`, `tools/build_codemap.py`, `tools/gen_amd64_status_inc.py`,
  `tools/ntfspatch.py`, `tools/toolchain.lock`


**Documentation (`docs/`, root)**
- all `docs/*.md`
- `docs/m0/` (except `EvcrtMsg.h.generated`, generated from Microsoft's message file)
- `README.md`, `PHASES.md`, `STATE.md`, `MODERNIZATION_PLAN.md`,
  `STANDING_INSTRUCTIONS.md`
- `docs/OPEN-REPLACEMENT-PLAN.md`, `docs/TASK-REQUIREMENTS.md`

## Third-party or unresolved-provenance files in the repository

**Removed historical XP-derived experiments (`kernel/`, `boot/`)**
- Tracked Microsoft-derived source, headers, probes, tests and compiled products
  were removed from the checkout and local Git refs/tags.
- The cleaned history has been force-pushed to GitHub. Existing clones must be
  recreated or carefully reset before contributing.
- The ignored local `Source/` symlink points outside this repository and is not
  included in tracked files or releases.
- Current `kernel/` and `boot/` files are the original AK1–AK3 implementation
  listed above; no Microsoft code is used in the boot path.


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
