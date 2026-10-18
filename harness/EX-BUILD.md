| target | flags tried | exit | artifact | blocker (quoted) |
|---|---|---|---|---|
| base\ntos\ex\up | -Z (no -c) | 0 | Source/XPSP1/NT/base/ntos/ex/up/obj/i386/ex.lib (built) | None; -Z skips clean so tracked prebuilt exinit.obj preserved |
| base\ntos\mm\up | -Z | 0 | Source/XPSP1/NT/base/ntos/mm/up/obj/i386/mm.lib | None |
| base\ntos\up | -Z | 0 | none in obj/i386/ | "File not found" — directory needs sources.inc/setup, not missing prebuilt |

Smallest change: pass -Z not -cZ (BUILD_DEFAULT embeds -c which deletes tracked obj/ binaries). Use git checkout -- Source/... to restore, never git clean.
Artifact disk-only: ex.lib, mm.lib, restored exinit.obj (142847 B). No source deletion under Source/.
