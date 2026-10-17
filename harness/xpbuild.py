#!/usr/bin/env python3
"""
M0/M1 build harness for the leaked Windows XP SP1 tree.

Reads razzle `sources` metadata and drives llvm-mingw to build the target,
repairing the known legacy-vs-modern error classes automatically. Never
modifies the source tree: all rewrites happen in a scratch mirror.

Usage:
    ./harness xpbuild.py <tree-relative-dir>            # build one target
    ./harness xpbuild.py --list                         # list buildable targets
    ./harness xpbuild.py --scan N                       # build up to N targets, report
    ./harness xpbuild.py --report                       # show the ledger

Exit codes: 0 built, 1 build failed, 2 harness/internal error.
"""

from __future__ import annotations

import argparse
import json
import os
import re
import shutil
import subprocess
import sys
from dataclasses import dataclass, field
from pathlib import Path

TREE = Path("/home/marocos/winxp_source/Source/XPSP1/NT")
WORK = Path("/tmp/xpbuild")
LEDGER = WORK / "ledger.jsonl"
MINGW = Path.home() / ".local/opt/llvm-mingw-20261006-msvcrt-ubuntu-22.04-x86_64/bin"
CC = MINGW / "i686-w64-mingw32-clang"
OBJCOPY = MINGW / "i686-w64-mingw32-objcopy"

# MS lib name -> mingw import library name.
LIBS = {
    "advancedapi": "advapi32", "advapi32": "advapi32", "advapi32l": "advapi32",
    "kernel32": "kernel32", "user32": "user32", "gdi32": "gdi32",
    "shell32": "shell32", "shlwapi": "shlwapi", "ole32": "ole32", "oleaut32": "oleaut32",
    "mpr": "mpr", "secur32": "secur32", "ws2_32": "ws2_32", "netapi32": "netapi32",
    "userenv": "userenv", "version": "version", "comctl32": "comctl32",
    "comdlg32": "comdlg32", "winspool": "winspool", "winmm": "winmm",
    "crypt32": "crypt32", "wintrust": "wintrust", "dnsapi": "dnsapi",
    "iphlpapi": "iphlpapi", "setupapi": "setupapi", "msvcrt": None, "ntdll": None,
    "uuid": "uuid", "rpcrt4": "rpcrt4", "oleacc": "oleacc", "psapi": "psapi",
    "odbc32": "odbc32", "odbcp32": "odbc32", "wtsapi32": "wtsapi32",
    "netapi32l": "netapi32", "activeds": "activeds", "adsiid": "adsiid",
}

# Error classes the harness knows how to repair.
CLASSES = {
    "C1_case": "filename case mismatch (Windows tolerated, Linux does not)",
    "C2_mc": "message-table header not generated from .mc",
    "C3_msasm": "MSVC inline asm / intrinsics (clang -fms-extensions)",
    "C4_abi": "in-tree SDK headers lack dllimport decoration -> use toolchain headers",
    "C5_comdat": "__inline emits multiple definitions",
    "C6_pch": "headers MSVC pulled in implicitly via the precompiled header",
    "C7_entry": "entry point naming (_wmain vs main)",
    "C8_unicode_macro": "UNICODE/_UNICODE not propagated",
    "C9_missing_header": "header exists in-tree under a different include path",
    "C10_unknown": "unclassified - needs investigation",
    "C11_sibling_library": "symbols supplied by a sibling in-tree library",
    "C12_cxx": "C++ target: needs the C++ stdlib and MSVC compatibility mode",
    "C13_nt_native": "native NT headers (nt.h etc.) included by user-mode pch; needs a DDK type shim (M1-class)",
}


@dataclass
class Target:
    rel: str                      # tree-relative dir
    name: str = ""
    type: str = "PROGRAM"         # PROGRAM | DYNLINK | LIBRARY | DRIVER
    sources: list[str] = field(default_factory=list)
    sources_from_lib: list[str] = field(default_factory=list)
    defines: list[str] = field(default_factory=list)
    libs: list[str] = field(default_factory=list)
    includes: list[str] = field(default_factory=list)
    entry: str = ""
    subsystem: str = ""
    pch: str = ""
    raw: dict = field(default_factory=dict)

    @property
    def has_c(self) -> bool:
        return any(s.lower().endswith((".c", ".cpp", ".cxx")) for s in self.sources)


def resolve_source(directory: Path, name: str) -> Path | None:
    """Resolve a `sources` entry to a real file on a case-sensitive filesystem."""
    direct = directory / name
    if direct.is_file():
        return direct
    stem = re.sub(r"\.[^.]+$", "", name).lower()
    code = [p for p in directory.iterdir() if p.suffix.lower() in (".c", ".cpp", ".cxx")]
    for p in code:
        if re.sub(r"\.[^.]+$", "", p.name).lower() == stem:
            return p
    return None


def read_sources(rel: str) -> Target | None:
    """Parse a razzle `sources` file into a Target."""
    p = TREE / rel / "sources"
    if not p.is_file():
        return None
    text = p.read_text(encoding="latin-1")

    def var(key: str) -> str:
        m = re.search(rf"^\s*{key}\s*=[ \t]*(.*)$", text, re.M)
        if not m:
            return ""
        parts = [m.group(1)]
        rest = text[m.end():].split("\n")[1:]
        for line in rest:
            stripped = line.strip()
            if not stripped:
                break
            if re.match(r"^[A-Z_]+\s*=", stripped):
                break
            parts.append(stripped)
            if not stripped.endswith("\\"):
                break
        val = " ".join(p.rstrip("\\").strip() for p in parts)
        return val.split("!")[0].split("#")[0].strip()

    t = Target(rel=rel)
    t.name = var("TARGETNAME")
    t.type = (var("TARGETTYPE") or "PROGRAM").upper()
    t.entry = var("UMENTRY")
    t.subsystem = (var("UMTYPE") or "").upper()
    t.pch = var("PRECOMPILED_CXX") or var("PRECOMPILED_C")

    for key in ("SOURCES", "SOURCES_LIBRARY", "SOURCES_SHARED"):
        for tok in var(key).split():
            if re.search(r"\.(c|cpp|cxx)$", tok, re.I) and not tok.startswith("$"):
                t.sources.append(tok)
            elif re.search(r"\.(c|cpp|cxx)$", tok, re.I):
                t.sources_from_lib.append(tok)

    t.defines = [d for d in var("C_DEFINES").split() if d.startswith("-D")]
    t.defines += [d for d in var("MSC_WARNING_LEVEL").split() if d.startswith("-D")]

    for tok in var("TARGETLIBS").split():
        base = Path(tok.replace("\\", "/")).name.lower()
        if base.endswith(".lib"):
            t.libs.append(base[:-4])

    t.raw = {"TARGETNAME": t.name, "TARGETTYPE": t.type, "SOURCES": t.sources,
             "TARGETLIBS": t.libs, "UMENTRY": t.entry}
    return t


def find_header_index() -> dict[str, list[Path]]:
    """Case-insensitive index of every header in the tree (for C1/C9)."""
    idx: dict[str, list[Path]] = {}
    for p in TREE.rglob("*.h"):
        idx.setdefault(p.name.lower(), []).append(p)
    return idx


HDR_INDEX: dict[str, list[Path]] | None = None


def headers() -> dict[str, list[Path]]:
    global HDR_INDEX
    if HDR_INDEX is None:
        cache = WORK / "hdr_index.json"
        if cache.is_file():
            try:
                HDR_INDEX = {k: [Path(x) for x in v] for k, v in json.loads(cache.read_text()).items()}
            except Exception:
                HDR_INDEX = None
        if HDR_INDEX is None:
            HDR_INDEX = find_header_index()
            WORK.mkdir(parents=True, exist_ok=True)
            cache.write_text(json.dumps({k: [str(x) for x in v] for k, v in HDR_INDEX.items()}))
    return HDR_INDEX


def classify(stderr: str) -> list[str]:
    """Map compiler output onto known error classes."""
    found = []
    if re.search(r"fatal error: '[^']+' file not found", stderr):
        found.append("C9_missing_header")
    if re.search(r"error: use of undeclared identifier", stderr):
        found.append("C6_pch")
    if re.search(r"duplicate symbol", stderr):
        found.append("C5_comdat")
    if re.search(r"__declspec\(dllimport\).*undefined", stderr):
        found.append("C4_abi")
    if re.search(r"expected 'volatile', 'inline', 'goto'", stderr):
        found.append("C3_msasm")
    if re.search(r"error: no such file or directory: '.*\.mc'", stderr):
        found.append("C2_mc")
    if not found:
        found.append("C10_unknown")
    return found


def gen_mc_header(mc: Path, out: Path) -> int:
    """Generate a message header from a .mc file (C2)."""
    text = mc.read_text(encoding="latin-1")
    cur = None
    syms: list[tuple[str, int]] = []
    for line in text.splitlines():
        mid = re.search(r"MessageId\s*=\s*([+\-]?\d+)", line)
        if mid:
            v = mid.group(1)
            if v.startswith("+"):
                if cur is not None:
                    cur += int(v[1:])
            elif cur is None or not v.startswith("-"):
                cur = int(v)
        sym = re.search(r"SymbolicName\s*=\s*([A-Za-z0-9_]+)", line)
        if sym and cur is not None:
            syms.append((sym.group(1), cur))
    if not syms:
        return 0
    guard = re.sub(r"[^A-Z0-9]", "_", out.name.upper())
    # .mc files also carry plain #define lines (e.g. an event-source name) that
    # the generated header must contain.
    extra = []
    for line in text.splitlines():
        m = re.match(r"\s*;#define\s+(.+)$", line)   # commented in body, emitted by mc
        if m:
            extra.append(f"#define {m.group(1).strip()}")
    body = "\n".join(extra + [f"#define {n} {v}L" for n, v in syms])
    out.write_text(f"#ifndef {guard}\n#define {guard}\n{body}\n#endif\n")
    return len(syms)


TOOLCHAIN_OWNED = {"windows", "winbase", "winnt", "windef", "winuser", "wingdi",
                   "stdio", "stdlib", "string", "wchar", "tchar", "security", "secext",
                   "lm", "lmaccess", "lmapibuf", "lmerr", "sspi", "winerror",
                   "uint_ptr", "int_ptr"}


def prepare_any(src_dir: Path, work: Path) -> dict:
    """Apply C1 (case) and C2 (.mc) repairs for an arbitrary source dir."""
    info = {"classes": set()}
    casemap = work / "casemap"
    casemap.mkdir(parents=True, exist_ok=True)
    gen = work / "gen"
    gen.mkdir(parents=True, exist_ok=True)
    for mc in src_dir.glob("*.mc"):
        stem = re.sub(r"\.[^.]+$", "", mc.name)
        n = gen_mc_header(mc, gen / (stem + ".h"))
        if n:
            info["classes"].add("C2_mc")
    for f in list(src_dir.iterdir()):
        if f.is_file() and f.suffix.lower() in (".h", ".rc", ".inc"):
            if f.stem.lower() in TOOLCHAIN_OWNED:
                continue
            for alias in {f.name, f.name.lower(), f.name.capitalize()}:
                dst = casemap / alias
                if not dst.exists():
                    try:
                        shutil.copy(f, dst)
                    except Exception:
                        pass
    for src in list(src_dir.glob("*.c")) + list(src_dir.glob("*.cpp")) + list(src_dir.glob("*.h")):
        try:
            text = src.read_text(encoding="latin-1")
        except Exception:
            continue
        for m in re.finditer(r'#include\s+["<]([^">]+)[">]', text):
            want = Path(m.group(1).replace("\\", "/")).name
            if Path(want).stem.lower() in TOOLCHAIN_OWNED:
                continue
            if (casemap / want).exists():
                continue
            hits = headers().get(Path(want).name.lower())
            if hits:
                shutil.copy(hits[0], casemap / want)
                info["classes"].add("C1_case")
            else:
                # C14: the header is generated from a .mc file elsewhere in the tree
                for mc in TREE.glob(f"**/{Path(want).stem.lower()}.mc"):
                    if gen_mc_header(mc, casemap / want):
                        info["classes"].add("C2_mc")
                        break
    return info


def prepare(target: Target, work: Path) -> tuple[list[Path], dict]:
    """Create the scratch mirror with C1/C2/C6 repairs applied."""
    info: dict = {"classes": set(), "units": []}
    src_dir = TREE / target.rel
    inc = [work / "inc"]
    (work / "inc").mkdir(parents=True, exist_ok=True)

    # C1: case-insensitive mirrors for every header the target names.
    casemap = work / "casemap"
    casemap.mkdir(exist_ok=True)
    for p in src_dir.glob("*"):
        if p.suffix.lower() in (".h", ".rc", ".inc"):
            link = casemap / p.name
            if link.name != p.name or not link.exists():
                for cand in p.parent.glob("*"):
                    if cand.name.lower() == p.name.lower() and cand.name != p.name:
                        target_name = casemap / cand.name
                        if not target_name.exists():
                            shutil.copy(cand, target_name)
    # Share one repair path with prepare_any (case folding + .mc generation).
    extra_info = prepare_any(src_dir, work)
    info["classes"] |= extra_info["classes"]

    # C2: generate headers for .mc files in the target dir.
    gen = work / "gen"
    gen.mkdir(exist_ok=True)
    for mc in src_dir.glob("*.mc"):
        stem = re.sub(r"\.[^.]+$", "", mc.name)
        n = gen_mc_header(mc, gen / (stem + ".h"))
        if n:
            # the tree also refers to it with 'Evcrtmsg.h' / 'EvcrtMsg.h' casing
            for variant in {stem.capitalize(), stem.capitalize().replace("crtmsg", "crtMsg")}:
                shutil.copy(gen / (stem + ".h"), gen / (variant + ".h"))
            info["classes"].add("C2_mc")
            info["mc_defines"] = n

    # C13: native NT headers are kernel-only. Provide stand-ins that define the
    # MSVC macro vocabulary before delegating the real Windows types to winnt.h.
    ntstub = work / "ntstub"
    ntstub.mkdir(exist_ok=True)
    prelude = ("#pragma once\n"
               "#include <windows.h>\n"
               "#ifndef _NT_STUB_TYPES_\n#define _NT_STUB_TYPES_\n"
               "typedef LONG NTSTATUS;\n"
               "typedef ULONG_PTR UINT_PTR, *PUINT_PTR;\n"
               "typedef LONG_PTR LONG_PTR;\n"
               "typedef ULONG_PTR DWORD_PTR, *PDWORD_PTR;\n"
               "typedef ULONG_PTR SIZE_T, *PSIZE_T;\n"
               "typedef LONG_PTR SSIZE_T, *PSSIZE_T;\n"
               "typedef struct _UNICODE_STRING { USHORT Length; USHORT MaximumLength; PWSTR Buffer; }"
               " UNICODE_STRING, *PUNICODE_STRING;\n"
               "typedef UNICODE_STRING STRING, *PSTRING, ANSI_STRING, *PANSI_STRING;\n"
               "typedef struct _IO_STATUS_BLOCK { union { NTSTATUS Status; PVOID Pointer; }; ULONG_PTR Information; }"
               " IO_STATUS_BLOCK, *PIO_STATUS_BLOCK;\n"
               "typedef struct _OBJECT_ATTRIBUTES { ULONG Length; HANDLE RootDirectory; UNICODE_STRING *ObjectName;"
               " ULONG Attributes; PVOID SecurityDescriptor; PVOID SecurityQualityOfService; }"
               " OBJECT_ATTRIBUTES, *POBJECT_ATTRIBUTES;\n"
               "typedef struct _CLIENT_ID { PVOID UniqueProcess; PVOID UniqueThread; } CLIENT_ID, *PCLIENT_ID;\n"
               "#endif\n"
               "#define STATUS_SUCCESS ((NTSTATUS)0x00000000L)\n"
               "#define STATUS_OBJECT_NAME_NOT_FOUND ((NTSTATUS)0xC0000034L)\n"
               "#define STATUS_OBJECT_PATH_NOT_FOUND ((NTSTATUS)0xC000003AL)\n"
               "#define STATUS_UNSUCCESSFUL ((NTSTATUS)0xC0000001L)\n"
               "#define STATUS_BUFFER_TOO_SMALL ((NTSTATUS)0xC0000023L)\n"
               "#define STATUS_ACCESS_DENIED ((NTSTATUS)0xC0000022L)\n"
               "#define STATUS_INVALID_PARAMETER ((NTSTATUS)0xC000000DL)\n")
    for h in ("nt", "ntrtl", "nturtl", "ntexapi", "ntimage", "zwapi", "ntosdef",
              "ntdef", "hal", "ntstatus"):
        (ntstub / f"{h}.h").write_text(prelude)
    (work / "ntstub_flag").touch()

    # C6: a wrapper TU supplies headers MSVC got implicitly from pch.h.
    extra = work / "extra.h"
    extra.write_text(
        "#ifndef _XP_BUILD_EXTRA_H_\n#define _XP_BUILD_EXTRA_H_\n"
        "#ifndef SECURITY_WIN32\n#define SECURITY_WIN32 1\n#endif\n"
        "#endif\n"
    )
    # C1b: MSVC source uses TitleCase header names; toolchain headers are lowercase.
    for want, real in (("Security.h", "security.h"), ("SecExt.h", "secext.h"),
                       ("Windows.h", "windows.h"), ("WinBase.h", "winbase.h"),
                       ("Stdio.h", "stdio.h"), ("String.h", "string.h"),
                       ("Tchar.h", "tchar.h"), ("WinNls.h", "winnls.h")):
        f = casemap / want
        if not f.exists():
            f.write_text(f'#pragma once\n#include <{real}>\n')

    # C9: pch files include neighbours by bare name; add each owner directory.
    extra_dirs: list[Path] = []
    for src in list(src_dir.glob("*.c")) + list(src_dir.glob("*.cpp")) + list(src_dir.glob("*.h")):
        try:
            text = src.read_text(encoding="latin-1")
        except Exception:
            continue
        for m in re.finditer(r'#include\s+[<"]([^>"]+)[>"]', text):
            want = Path(m.group(1).replace("\\", "/")).name.lower()
            if Path(want).stem.lower() in TOOLCHAIN_OWNED:
                continue  # never let a tree dir shadow the toolchain
            for hit in headers().get(want, [])[:1]:
                if "public/sdk" in str(hit) or "public/internal" in str(hit):
                    continue
                if hit.parent in extra_dirs or hit.parent == src_dir:
                    continue
                clash = any(f.stem.lower() in TOOLCHAIN_OWNED
                            for f in hit.parent.glob("*.h"))
                if clash:
                    if os.environ.get("XPDEBUG"):
                        print(f"  C9 skips {hit.parent}: holds toolchain-shadowing headers")
                    continue
                if os.environ.get("XPDEBUG"):
                    print(f"  C9 adds {hit.parent} for <{want}>")
                extra_dirs.append(hit.parent)
    extra_dirs = extra_dirs[:24]

    return [inc, casemap, gen, ntstub, src_dir,
            TREE / "public/internal/admin/inc"] + extra_dirs, info


def compile_unit(src: Path, target: Target, work: Path, incdirs: list[Path], defines: list[str],
                 extra_flags: list[str]) -> tuple[bool, str]:
    """Compile one translation unit, repairing C6 by rewriting the pch include."""
    obj = work / (re.sub(r"\.[^.]+$", "", src.name) + ".o")
    include_flags: list[str] = []
    for d in incdirs:
        for part in (d if isinstance(d, (list, tuple)) else [d]):
            include_flags.append(f"-I{part}")

    base = [str(CC), "-fms-extensions", "-c", "-o", str(obj), "-Wno-everything",
            "-D_M_IX86=500", "-D_X86_=1", "-DSECURITY_WIN32=1", "-DCONST=const",
            ] + (["-fpermissive", "-Wno-error"] if src.suffix.lower() in (".cpp", ".cxx") else []) + defines + include_flags + extra_flags

    text = src.read_text(encoding="latin-1")
    pch_includes = re.findall(r'^\s*#include\s+"(pch\.h)"', text, re.M)
    attempts: list[Path] = [src]

    patch = None
    if pch_includes or 'pch.h' in text:
        # C6: replace the pch include with the headers it really provided.
        patch = work / f"{src.stem}_xpbuild.c"
        lines = []
        for line in text.split("\n"):
            if re.match(r'\s*#include\s+"pch\.h"', line):
                lines.append('#define SECURITY_WIN32 1')
                lines.append('#include <stdio.h>')
                lines.append('#include <stdlib.h>')
                lines.append('#include <string.h>')
                lines.append('#include <tchar.h>')
                lines.append('#include <windows.h>')
                lines.append('/* native NT headers (nt.h, ntrtl.h, nturtl.h, ntexapi.h) are')
                lines.append('   kernel/ntdll-only; user-mode code uses the Win32 equivalents */')
                lines.append('#include <security.h>')
                lines.append('#include <lm.h>')
                lines.append('#include <lmaccess.h>')
                lines.append('#include <lmapibuf.h>')
                lines.append('#include <lmerr.h>')
            else:
                lines.append(line)
        patch.write_text("\n".join(lines), encoding="latin-1")
        attempts.insert(0, patch)

    last = ""
    for attempt in attempts:
        lang = ["-x", "c"] if src.suffix.lower() == ".c" else []
        cmd = base + lang + [str(attempt)]
        if os.environ.get("XPDEBUG"):
            print("CMD:", " ".join(cmd))
        proc = subprocess.run(cmd, capture_output=True, text=True)
        if proc.returncode == 0:
            return True, ""
        last = proc.stderr
        if "fatal error:" not in last:
            break
        if re.search(r"fatal error: '(nt|ntrtl|nturtl|ntexapi|ntimage)\.h' file not found", last):
            stub = work / "ntstubs"
            stub.mkdir(exist_ok=True)
            for hh in ("nt", "ntrtl", "nturtl", "ntexapi", "ntimage"):
                ff = stub / f"{hh}.h"
                if not ff.exists():
                    ff.write_text("#pragma once\n#include <winnt.h>\n")
            base += [f"-I{stub}"]
            lang = ["-x", "c"] if src.suffix.lower() == ".c" else []
            proc = subprocess.run(base + lang + [str(attempt)], capture_output=True, text=True)
            if proc.returncode == 0:
                return True, ""
    return False, last


def resolve_library_objects(stderr: str, work: Path, defines: list[str],
                            target_dir: str | None = None) -> tuple[list[Path], str]:
    """Build the in-tree library directories that define unresolved symbols.

    Razzle libraries are just directories with a `sources` file; there is no
    prebuilt .lib for a tree-internal library, so we compile it.
    """
    subs = re.findall(r"undefined symbol: _?([A-Za-z_][A-Za-z0-9_]*)", stderr)
    subs = [x for x in dict.fromkeys(subs) if not x.startswith(("__", "std", "_Z"))]
    if not subs:
        return [], ""

    # Search only the target's own subtree: cmdline binaries live next to their
    # libraries, and a global search picks up unrelated namesakes.
    roots = [str(TREE / target_dir)] if target_dir else []
    homes: set[Path] = set()
    for sym in subs:
        for root in roots:
            proc = subprocess.run(
                ["grep", "-rlE", rf"^[A-Za-z_].*\b{sym}\s*\(", root,
                 "--include=*.c", "--include=*.cpp"],
                capture_output=True, text=True,
            )
            for f in proc.stdout.strip().splitlines()[:2]:
                homes.add(Path(f).parent)
    # keep only dirs that look like razzle libraries (have sources/, no PROGRAM)
    libs = []
    for h in homes:
        t = read_sources(str(h.relative_to(TREE))) if (h / "sources").is_file() else None
        if t and t.type in ("LIBRARY", "DYNLINK", "STATIC_LIBRARY") and t.has_c:
            libs.append(t)

    objs: list[Path] = []
    for lib in libs:
        sub_work = work / ("lib_" + re.sub(r"[^A-Za-z0-9]", "_", lib.rel))
        sub_work.mkdir(parents=True, exist_ok=True)
        prepare_any(TREE / lib.rel, sub_work)
        incdirs = [sub_work / "casemap", sub_work / "gen", work / "inc",
                   work / "casemap", TREE / lib.rel, TREE / "public/internal/admin/inc"]
        for name in lib.sources:
            src = resolve_source(TREE / lib.rel, name)
            if src is None:
                continue
            ok, err = compile_unit(src, lib, sub_work, incdirs,
                                   defines + ["-DUNICODE", "-D_UNICODE"], [])
            obj = sub_work / (re.sub(r"\.[^.]+$", "", src.name) + ".o")
            if ok and obj.is_file():
                objs.append(obj)
            elif os.environ.get("XPDEBUG"):
                bad = [l for l in err.splitlines() if "error:" in l]
                print(f"  lib unit failed: {src.name}: {bad[0][:110] if bad else '?'}")
    return objs, ("C11_sibling_library" if objs else "")


def build(rel: str, verbose: bool = False) -> dict:
    t = read_sources(rel)
    if t is None:
        return {"target": rel, "status": "no_sources"}
    if not t.has_c:
        return {"target": rel, "status": "skipped_no_source", "name": t.name}

    work = WORK / "build" / re.sub(r"[^A-Za-z0-9_.-]", "_", rel)
    if work.exists():
        shutil.rmtree(work)
    work.mkdir(parents=True)

    incdirs, info = prepare(t, work)
    classes: set[str] = set(info["classes"])

    defines = [d for d in t.defines if not d.startswith("-D_")] + ["-DUNICODE", "-D_UNICODE"]
    objs: list[Path] = []
    failed = None

    for name in t.sources:
        src = resolve_source(TREE / t.rel, name)
        if src is None:
            # C1 in the SOURCES list itself: match by stem, ignoring the
            # extension too, because razzle says .cpp where the tree has .c
            cand = None
            if cand is None:
                classes.add("C1_case")
                failed = [f"source not found: {name}"]
                break
            src = cand
            classes.add("C1_case")

        ok, err = compile_unit(src, t, work, incdirs, defines, [])
        if not ok:
            # One C6 retry: the TU needs a header MSVC supplied implicitly.
            ok2, err2 = compile_unit(src, t, work, incdirs,
                                     defines + ["-include", str(work / "extra.h")], [])
            if ok2:
                classes.add("C6_pch")
            else:
                classes |= set(classify(err2 or err))
                _raw = (err2 or err)
                failed = [l for l in _raw.strip().split("\n") if "error" in l][:3] or _raw.strip().split("\n")[:3]
                if os.environ.get("XPERR"):
                    print("RAW:", _raw[:1200])
                break
        objs.append(work / (re.sub(r"\.[^.]+$", "", src.name) + ".o"))

    result = {
        "target": rel, "name": t.name, "type": t.type,
        "sources": len(t.sources), "classes": sorted(classes),
    }

    if failed:
        result["status"] = "compile_failed"
        result["error"] = failed
        result["raw_error"] = (last_err := (err2 or err)) if False else ""
        if verbose:
            print(f"\n  compile failed: {t.rel}")
            for line in (failed if isinstance(failed, list) else [failed]):
                print(f"    {line}")
            if os.environ.get("XPERR"):
                print("  ---- raw ----")
                print((result.get("raw_error") or "")[:2500])
        return result

    # Link.
    is_dll = t.type in ("DYNLINK", "SHARED_LIBRARY")
    exe = work / (t.name or "out").lower().replace(" ", "_")
    exe = exe.with_suffix(".dll" if is_dll else ".exe")
    link = [str(CC), "-o", str(exe), "-Wl,--allow-multiple-definition"]
    if t.type in ("LIBRARY", "STATIC_LIBRARY"):
        # nothing to link: the object files ARE the artifact
        result = {"target": t.rel, "name": t.name, "type": t.type,
                  "sources": len(t.sources), "classes": sorted(classes),
                  "status": "built", "exe": str(objs[0]) if objs else "",
                  "exe_size": sum(o.stat().st_size for o in objs),
                  "exe_kind": f"{len(objs)} object files", "units": len(objs)}
        return result
    if is_dll:
        link += ["-shared"]
    else:
        link += ["-mconsole"] if t.subsystem in ("", "CONSOLE") else ["-mwindows"]
    link += [str(o) for o in objs]
    mapped = [LIBS[lib] for lib in t.libs if lib in LIBS and LIBS[lib]]
    link += [f"-l{l}" for l in dict.fromkeys(mapped)]

    proc = subprocess.run(link, capture_output=True, text=True)

    # C11: resolve symbols that live in a sibling in-tree library.
    if proc.returncode != 0 and "undefined symbol" in proc.stderr:
        extra_objs, extra_class = resolve_library_objects(proc.stderr, work, defines,
                                                          str(Path(t.rel).parent))
        if os.environ.get("XPDEBUG"):
            print("C11 objects:", [str(o) for o in extra_objs][:4])
        if extra_objs:
            classes.add(extra_class)
            link = link + [str(o) for o in extra_objs]
            proc = subprocess.run(link, capture_output=True, text=True)

    # C7: mingw's CRT wants `main`; MS mangles `_wmain`. Rename only the object
    # that actually defines the entry point.
    if proc.returncode != 0 and "undefined symbol: _WinMain@16" in proc.stderr:
        classes.add("C7_entry")
        # GUI target: MSVC compiled main/wmain into WinMain@16
        for o in objs:
            nm = subprocess.run([str(MINGW / "i686-w64-mingw32-nm"), str(o)],
                                capture_output=True, text=True).stdout
            for sym in ("_main", "_wmain"):
                if re.search(rf"\bT {sym}\b", nm):
                    ro = o.with_name(o.stem + "_gui.o")
                    subprocess.run([str(OBJCOPY), "--redefine-sym",
                                    f"{sym}=_WinMain@16", str(o), str(ro)], capture_output=True)
                    if ro.exists() and ro.stat().st_size > 1024:
                        link = [str(ro) if x == str(o) else x for x in link]
                        proc = subprocess.run(link, capture_output=True, text=True)
                    break

    if proc.returncode != 0 and "undefined symbol: _wmainCRTStartup" in proc.stderr:
        classes.add("C7_entry")
        for o in objs:
            defs = subprocess.run([str(OBJCOPY), "-O", "binary", "/dev/null", "/dev/null"],
                                  capture_output=True)
            nm = subprocess.run([str(MINGW / "i686-w64-mingw32-nm"), str(o)],
                                capture_output=True, text=True).stdout
            if not re.search(r"\bT _wmain\b", nm):
                continue
            ro = o.with_name(o.stem + "_ent.o")
            subprocess.run([str(OBJCOPY), "--redefine-sym", "_wmain=_main",
                            str(o), str(ro)], capture_output=True)
            if ro.exists() and ro.stat().st_size > 1024:
                link = [str(ro) if x == str(o) else x for x in link]
                proc = subprocess.run(link, capture_output=True, text=True)
            break

    if proc.returncode != 0:
        result["status"] = "link_failed"
        result["classes"] = sorted(classes)
        result["error"] = proc.stderr.strip().split("\n")[:4]
        if verbose:
            print(f"\n  link failed: {t.rel}")
            for line in result["error"]:
                print(f"    {line}")
        return result

    result["status"] = "built"
    result["classes"] = sorted(classes)
    result["exe"] = str(exe)
    result["exe_size"] = exe.stat().st_size
    result["exe_kind"] = subprocess.run(["file", "-b", str(exe)], capture_output=True,
                                        text=True).stdout.strip()[:80]

    # Smoke run under Wine for console programs.
    if result["exe_kind"].startswith("PE32 executable") and "console" in result["exe_kind"]:
        try:
            run = subprocess.run(["timeout", "25", "wine", str(exe)], capture_output=True, text=True)
            result["wine_exit"] = run.returncode
        except Exception:
            result["wine_exit"] = None
    return result


def discover(limit: int | None = None) -> list[str]:
    """Find tuaret directories that have a `sources` file with C/C++ code."""
    found = []
    for src in TREE.rglob("sources"):
        rel = src.parent.relative_to(TREE)
        if any(part in ("alpha", "axp64", "ia64", "amd64", "wow6432") for part in rel.parts):
            continue
        found.append(str(rel))
    found.sort()
    return found[:limit] if limit else found


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("target", nargs="?", help="tree-relative directory containing `sources`")
    ap.add_argument("--scan", type=int, default=0, help="build up to N discovered targets")
    ap.add_argument("--user-mode-only", action="store_true", help="skip DRIVER/LIBRARY targets")
    ap.add_argument("--report", action="store_true", help="print the ledger summary")
    ap.add_argument("-v", "--verbose", action="store_true")
    args = ap.parse_args()

    WORK.mkdir(parents=True, exist_ok=True)

    if args.report:
        rows = [json.loads(l) for l in LEDGER.read_text().splitlines()] if LEDGER.is_file() else []
        built = [r for r in rows if r.get("status") == "built"]
        print(f"ledger: {len(rows)} targets, {len(built)} built")
        counts: dict[str, int] = {}
        for r in rows:
            for c in r.get("classes", []):
                counts[c] = counts.get(c, 0) + 1
        for c, n in sorted(counts.items(), key=lambda kv: -kv[1]):
            print(f"  {c:20} {n:5}  {CLASSES.get(c, '')}")
        return 0

    targets = [args.target] if args.target else discover(args.scan or 0)
    if not targets:
        print("no targets", file=sys.stderr)
        return 2

    results = []
    for rel in targets:
        r = build(rel, verbose=args.verbose)
        if args.user_mode_only and r.get("status") == "skipped_no_source":
            pass
        results.append(r)
        if r.get("status") == "built":
            print(f"BUILT   {rel}  ({r.get('exe_size', 0)} bytes)  classes={','.join(r.get('classes') or ['-'])}")
        elif r.get("status") == "compile_failed":
            print(f"COMPILE {rel}  {r.get('classes')}: {(r.get('error') or [''])[0][:80]}")
        elif r.get("status") == "link_failed":
            print(f"LINK    {rel}  {r.get('classes')}: {(r.get('error') or [''])[0][:80]}")
        else:
            print(f"{r.get('status').upper()} {rel}")

    with LEDGER.open("a") as fh:
        for r in results:
            fh.write(json.dumps(r) + "\n")

    built = sum(1 for r in results if r.get("status") == "built")
    print(f"\n{built}/{len(results)} built")
    return 0 if built else 1


if __name__ == "__main__":
    sys.exit(main())
