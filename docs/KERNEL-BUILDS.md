# The XP kernel executable builds from source

**`ntoskrnl.exe` — 1,918,976 bytes, PE32 native i386 — produced by the tree's own
toolchain from the tree's own sources.**

## The linker recipe

The link step is *not* missing from the leak. Razzle performs it inside
`base/ntos/init/up` when `NTTARGETFILE1` selects the test target. The chain:

```
base\ntos\init\up/sources.cmn:
  TARGETTYPE=LIBRARY          (init.lib)
  NTTARGETFILE1=$(DDK_LIB_PATH)\ntoskrnl.lib
  EXPORT_FILE=$(O)\ntoskrnl.exp
```

So razzle first builds `init.lib`, then runs `lib.exe` to produce
`ntoskrnl.lib` / `ntoskrnl.exp` from the 26 component libraries, then links the
executable.

### Dependencies discovered while linking

Two boot-layer libraries are needed by the link and are NOT under `base/ntos`:

| missing lib | built from |
|---|---|
| `bootvid.lib` | `harness/razzle.sh 'base\boot\bootvid' -Z` |
| `kdcom.lib`  | `harness/razzle.sh 'base\boot\kdcom' -Z`  |

Order that works:

```sh
harness/razzle.sh 'base\boot\bootvid' -Z
harness/razzle.sh 'base\boot\kdcom'  -Z
harness/razzle.sh 'base\ntos\init\up' -Z      # -> ntoskrnl.exe
```

Success line:

```
Linking Executable - obj\i386\ntoskrnl.exe for i386
BUILD_RC=0
```

Output: `Source/XPSP1/binaries.x86fre/ntoskrnl.exe`

## Verification against the shipped kernel

Compared with `ntoskrnl.exe` extracted from the retail XP SP1 ISO:

| | ours | shipped |
|---|---:|---:|
| size | 1,918,976 | 2,042,240 |
| exports | **1,464** | **1,464** |
| export name set | **identical** | **identical** |

Zero names only in ours; zero names only in theirs.

Section sizes:

| section | ours | shipped |
|---|---:|---:|
| `.text` | 0x5d960 | 0x66651 |
| `.data` | 0x125a4 | 0x12880 |
| `.edata` | 0xb258 | 0xb258 |
| `.rsrc` | 0x14468 | 0x144a0 |
| `.reloc` | 0xf450 | 0xf08c |

`.edata` matches byte-for-byte in size; `.text` is smaller because this is a
`-Z` incremental build of a subset of optional components (no IA-64/AMD64 paths,
no debug symbol tables), not because code is missing from the export contract.

## Evidence kept

- `docs/kernel-build/ntoskrnl-built.exe` — our build
- `docs/kernel-build/ntoskrnl-shipped.exe` — Microsoft's, from the ISO
- `docs/kernel-build/link.log` — the razzle link output
