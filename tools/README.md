# tools/

## build_codemap.py

Turns graft's wiring graph into a browsable codebase map with per-file
dependencies.

```sh
# 1. build the graph (one time, ~4 min for the whole kernel)
mkdir -p /tmp/xpgraph && cd /tmp/xpgraph
cp -r /home/marocos/winxp_source/Source/XPSP1/NT/base/ntos ntos
graft build .

# 2. generate the map
cd /home/marocos/anemos
python3 tools/build_codemap.py
```

### Outputs

| Path | Contents |
|---|---|
| `docs/codemap/CODEMAP.md` | directory rollup, the 40 most-referenced symbols, widest-dependency files |
| `docs/codemap/files/<path>.md` | **one page per file**: imports, defines, calls out, called by |
| `docs/codemap/deps.tsv` | one row per file: `file  imports  defines  calls_out  called_by` |
| `docs/codemap/deps.json` | same data, machine readable, full symbol names |

### Coverage (measured)

1110 files, 1091 import edges, 5561 resolved call edges, 17,406 symbols.

Import edges are what graft extracts from `#include` lines; some headers are
reached through precompiled-header chains and so do not appear, which is why
`imports` is often 1 for a file that logically depends on several headers.
**`calls_out` / `called_by` are the more useful columns.**

### Known limits

- `.asm` routines are not parsed (`KiSwapThread` is invisible).
- Ambiguous symbol names are dropped rather than guessed, so counts can undercount.
This is graft's behaviour, documented in `docs/KNOWLEDGE-GRAPH.md`.
