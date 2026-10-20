# Phase tags

The project is one history, but each phase boundary is an annotated git tag.
That lets a phase be reviewed, diffed or pushed on its own without splitting
the repository or rewriting history.

| Tag | Snapshot | What it marks |
|---|---|---|
| `phase-p0` | `0492280` | P0 gates: G1 ABI proof (15/15) and G2 conformance runner. G2's extract-mode hardening follows in `7b719cc`. |
| `phase-p1` | `8e099cf` | P1 complete, 6/6: MD5, SHA-1, Base64, CRC-32, NLS, and `STATUS→DOS` proved against XP for all 2^32 statuses. |
| `phase-p2` | `f1381fd` | P2 complete: DNS codec/client and DHCP codec/client, each with an independent implementation as the oracle. |
| `phase-p3.0` | `3a7478e` | P3.0: OpenSSL 3.5.7 cross-built for i686 Windows; SHA/HMAC/AES/RSA KATs. |
| `phase-p3.1` | `255bd57` | P3.1: CSP SPI skeleton — 23+2 exports, contexts, MD5/SHA-1/SHA-256, `CPGenRandom`, provider params. |
| `phase-p3.1-xp` | `1f9e1ec` | P3.1 plus the XP hardening: no-threads OpenSSL rebuild and the import-surface gate against `xp-pristine.vmdk`. Current P3 state. |

P3 is **in progress**. There is deliberately no `phase-p3` tag yet; it will be
created when the P3 gate passes (symmetric keys, RSA, the XP run, the CryptoAPI
path, and the RAND-recursion proof).

## Which code belongs to which phase

| Phase | Code |
|---|---|
| P0 | `harness/conformance/`, `tools/abi/`, the two VMDK-based gates |
| P1 | `anemos/{md5,sha1,base64,crc32,nls,statusdos}/` |
| P2 | `anemos/dns/`, `anemos/dhcp/` |
| P3 | `anemos/crypto/`, `anemos/csp/` |

## Inspecting or pushing a phase

```sh
git show phase-p1                  # the commit and its diff
git diff phase-p1..phase-p2        # what P2 added on top of P1
git log --oneline phase-p2..phase-p3.0
git push origin phase-p1           # once a remote exists
git push --tags                    # publish all phase tags
```

The public remote is `origin`:
`https://github.com/NickLitharis/anemos`. The phase tags are already published
there. Push a new tag with `git push origin <tag>`, or republish all of them
with `git push --tags`.

Phases interleave in history: a tag marks **when the gate passed**, not a tree
containing only that phase's code. For example, `phase-p0` already contains some
early P1 units because P1 work started while G2 was still being hardened. Use
the table above, or the `anemos/` directory groups, when you need the code for
one phase rather than a point in time.
