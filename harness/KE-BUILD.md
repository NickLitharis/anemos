| target | exit | artifact? | first error (quoted) | class |
|---|---|---|---|---|
| base\ntos\ke\up | 0 | yes (ke.lib) | — | success |
| base\ntos\ex\up | 1 | no | "NMAKE : U1073: don't know how to make 'obj\i386\exinit.obj'" | missing source/object |
| base\ntos\mm\up | — | — | (not run — stopped) | — |
| base\ntos\up | — | — | (not run — stopped) | — |

Most important blocker: ex\up missing exinit.obj (NMAKE U1073) — kernel scheduler built, next layer needs source/object fix.
