# Code map: graft over the XP kernel

`graft` (already installed) builds a symbol/call graph from C sources and lets
that be queried instead of grepping. It answers "who calls this", "what does
this need", and "show me this file's API surface" in a handful of tokens.

## Setup (one command, ~4 minutes for the whole kernel)

```sh
mkdir -p /tmp/xpgraph && cd /tmp/xpgraph
cp -r /home/marocos/winxp_source/Source/XPSP1/NT/base/ntos ntos
graft build .
```

Measured result on the full kernel:

```
✓ wiring: 18516 nodes (15453 function, 1335 class, 1110 file, 618 type),
          11143 edges, 1045 cards     parsed: 1110 of 1110 files
```

(The RTL directory alone is 1,690 nodes / 1,319 edges; `kernel/` itself is
572 nodes / 177 edges.)

## The queries that matter

| Command | Answers |
|---|---|
| `graft callers SYM .` | who calls SYM, with file:line |
| `graft callers SYM . --direction out` | what SYM needs (dependency list) |
| `graft skeleton FILE .` | signatures only — cheapest API surface view |
| `graft ask "question" .` | ranked symbols/files |
| `graft file_api FILE .` | a file's exported surface |
| `graft build --deep` | adds LLM summaries (needs a model) |

## Worked example: the prefix.c blocker

This is the query that took me dozens of greps to approximate by hand:

```
$ graft callers RtlSplay .
RtlSplay · function · rtl/splay.c:L50-L381
  calls ← RtlEnumerateGenericTable       (rtl/gentable.c:812)
  calls ← RtlInsertElementGenericTableFull (rtl/gentable.c:516)
  calls ← RtlLookupElementGenericTableFull (rtl/gentable.c:726)
  calls ← PfxFindPrefix                  (rtl/prefix.c:716)
  calls ← PfxInsertPrefix                (rtl/prefix.c:386)
  calls ← RtlFindUnicodePrefix           (rtl/prefix.c:1866)
  calls ← RtlInsertUnicodePrefix (rtl/prefix.c:1373)   <- the call that hangs
  calls ← RtlDelete                      (rtl/splay.c:460)
```

## Known limits (measured, not assumed)

- **`.asm` files are not parsed.** `KiSwapThread` and other assembly-level
  routines are invisible to the graph. For those, grep is still the right tool.
- **Ambiguous names are dropped, not guessed.** Three definitions of
  `KeWaitForSingleObject` exist; cross-file callers of an ambiguous name are
  omitted, so counts can undercount. The tool says so explicitly.
- **Only the extension set it supports** (`c`, `python`, seen in output). XP's
  `.cpp`/`.cxx` shell code may index less well than C.

## Why this matters for this project

The recurring cost in this work is dependency discovery: which symbol pulls in
which header, and therefore whether a kernel file is reachable. That is exactly
what the graph answers, in one call, with file:line precision.
