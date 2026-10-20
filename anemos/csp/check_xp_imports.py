#!/usr/bin/env python3
"""Fail if a PE imports a function XP's system DLLs do not export.

Wine implements Vista+ APIs, so a DLL can pass every KAT under Wine and still
fail to load on XP. This is the static half of the compatibility gate: parse
the PE import table and compare each name with the export table of the same
DLL on a pristine XP install.

The export lists in xp_exports/ were extracted from
`xp-pristine.vmdk` (Microsoft's kernel/OS, the oracle) with:

  python3 tools/ntfspatch.py xp-pristine.vmdk get \
      'windows\\system32\\<dll>' <tmp>/<dll>
  llvm-readobj --coff-exports <tmp>/<dll> | awk '/Name: /{print $2}'

They are facts about an artifact (the exported API surface), not source.

usage: check_xp_imports.py <pe> [--exports DIR] [--objdump PATH]
"""

from __future__ import annotations

import argparse
import re
import shutil
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
DEFAULT_EXPORTS = HERE / "xp_exports"


def find_objdump(explicit: str | None) -> str:
    if explicit:
        return explicit
    for name in ("llvm-objdump", "llvm-objdump-23", "objdump"):
        found = shutil.which(name)
        if found:
            return found
    raise SystemExit("cannot find llvm-objdump; pass --objdump")


def parse_imports(objdump: str, pe: Path) -> dict[str, list[str]]:
    proc = subprocess.run([objdump, "-p", str(pe)], capture_output=True,
                          text=True)
    if proc.returncode != 0:
        raise SystemExit(f"objdump failed on {pe}: {proc.stderr.strip()}")
    imports: dict[str, list[str]] = {}
    dll: str | None = None
    for line in proc.stdout.splitlines():
        header = re.match(r"\s*DLL Name:\s*(\S+)", line)
        if header:
            dll = header.group(1).lower()
            imports.setdefault(dll, [])
            continue
        entry = re.match(r"\s+\d+\s+(\S+)\s*$", line)
        if entry and dll is not None:
            imports[dll].append(entry.group(1))
    return imports


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("pe", type=Path)
    parser.add_argument("--exports", type=Path, default=DEFAULT_EXPORTS)
    parser.add_argument("--objdump", default=None)
    args = parser.parse_args(argv)

    objdump = find_objdump(args.objdump)
    imports = parse_imports(objdump, args.pe)
    if not imports:
        print(f"FAIL {args.pe}: no imports parsed")
        return 1

    missing_total = 0
    for dll in sorted(imports):
        base = dll.removesuffix(".dll")
        export_file = args.exports / f"{base}.txt"
        names = imports[dll]
        if not export_file.exists():
            print(f"FAIL {dll}: no XP export list ({export_file.name})")
            missing_total += 1
            continue
        exported = set(export_file.read_text(encoding="ascii").split())
        missing = sorted(n for n in names if n not in exported)
        if missing:
            print(f"FAIL {dll}: {len(missing)} import(s) XP does not export")
            for name in missing:
                print(f"    {name}")
            missing_total += len(missing)
        else:
            print(f"PASS {dll}: all {len(names)} imports exist on XP")

    if missing_total:
        print(f"FAILED: {missing_total} missing import(s)")
        return 1
    print("all imports are available on XP")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
