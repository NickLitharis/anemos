# NOTICE

## This repository contains two unrelated bodies of material

**1. Original work, MIT-licensed** — see `LICENSE` and `SCOPE.md`.
Everything in `harness/`, `tools/`, `kernel/`, `boot/`, `docs/` and the root
markdown files was written for this project.

**2. Third-party material this project does not own** — `Source/`.

## About `Source/`

`Source/` is **leaked proprietary source code belonging to Microsoft Corporation**
(Windows XP, NT 5.1). It is here as a local reference and test oracle.

The project makes no claim to it:

- It is **not** licensed under MIT or any other license by us.
- It is **not** distributed by this project.
- It is **not** a contribution, and contributed code must not be derived from it.
- Its presence does not make any other file in this repository a derivative work of it.

Anyone reproducing this environment should treat `Source/` as an external artifact
they obtain separately. **Publishing it is copyright infringement.**

## About the compiled binaries in `dist/` and `docs/`

`dist/kernel/*.exe|dll`, `docs/kernel-build/*.exe` and `docs/razzle-evidence/*.lib`
were *built by us* from Microsoft's source. The toolchain invocation and build
scripts are ours and are MIT. **The code inside those binaries is Microsoft's**
and is not licensed to you. They are committed so that the claim "this kernel
boots XP" can be checked independently, not as a distributable artifact.

## Clean-room policy for contributions

This project intends to ship code that is legally clean. That imposes rules:

1. **Specifications, not source.** A replacement is written against a public
   standard (RFC, NIST, POSIX, published ABI documentation) or against behaviour
   observed at runtime. Never by transcribing `Source/`.
2. **Observed behaviour is fair game.** "XP returns `STATUS_INVALID_PARAMETER`
   here" is a fact about an artifact, not a copyrightable expression.
3. **No mechanical transliteration.** Renaming variables, reordering statements,
   or reformatting Microsoft's C and calling it a rewrite still produces a
   derivative work. C23 syntax over Microsoft's logic is not clean-room code.
4. **Record provenance.** Every replacement file states what spec it implements
   and what it was tested against.

Point 3 is why "modernise the existing C" is not part of this project's plan:
it would be derivative by construction.

## Not legal advice

The above describes this project's policy. It is not legal advice. Whether any
given file is a derivative work is a question for a qualified lawyer, and anyone
distributing this work should get that answer before publishing.

## Contact

Open an issue.
