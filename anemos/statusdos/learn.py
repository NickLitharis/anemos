#!/usr/bin/env python3
"""Fold a divergent certificate's mismatches into model.txt.

The learning half of the model-verification loop:

    gen_model.py  ->  table.c  ->  mv_probe.exe (in the guest)
        ^                                    |
        |         learn.py <cert> <model>   |
        +-----------------------------------+

Each record says (status, what our model answered, what XP answered). The
correction is to make the model answer XP's value at that status — as a
table entry, unless the rules already produce it, in which case the entry
is *removed* (a wrong entry whose corrected value equals the rule output
is structure gen_model.py refuses anyway).

What this tool deliberately does NOT do: invent rules. If one quadrant
reports millions of mismatches, the divergence is rule-shaped and folding
entries would take forever and hide the bug — check_cert.py refuses such
certificates with that diagnosis, and this tool inherits the refusal.

Safety properties:
  - refuses a certificate check_cert.py would refuse: stale CRC/entry count,
    no footer, incomplete walk, capped quadrant, a record contradicting the
    model, non-ascending records (same checks, same order);
  - never writes without --write: the default is a dry run that reports the
    change count, so a typo cannot rewrite model.txt;
  - preserves every existing line's comment; entries are corrected in place,
    dropped entries take their indented continuation comments with them, and
    new entries are inserted in sorted position (not appended) — model.txt
    must stay ascending for gen_model.py.

usage: learn.py <model.txt> <sdos-cert.bin> [--write]
"""
import struct
import sys
import zlib

DEFAULT = 317
CUSTOMER = 0x20000000
WIN32_HI, WIN32_MASK = 0x80070000, 0xFFFF0000
HRESULT_HI, HRESULT_MASK, HRESULT_CLEAR = 0xD0000000, 0xF0000000, 0xCFFFFFFF
OS2_HI = 0xC001

HDR_MAGIC, FTR_MAGIC = b"SDMV", b"SDMX"
HDR_SIZE, FTR_SIZE, REC_SIZE = 16, 56, 12
NEW_COMMENT = "  # learned from an in-guest certificate"


def unmask(status: int) -> int:
    """The status the table is searched with: rule 3 clears the HRESULT bits,
    so a correction learned from a 0xD... status belongs on its 0xC... form."""
    if (status & HRESULT_MASK) == HRESULT_HI:
        return status & HRESULT_CLEAR
    return status


def rule_output(status: int) -> int:
    """statusdos.c's rules when the table has no entry for `status`."""
    if status & CUSTOMER:
        return status
    if (status & WIN32_MASK) == WIN32_HI:
        return status & 0xFFFF
    s = status
    if (s & HRESULT_MASK) == HRESULT_HI:
        s &= HRESULT_CLEAR
    if (s >> 16) == OS2_HI:
        return s & 0xFFFF
    return DEFAULT


def parse_model(path: str) -> list[tuple[int, int]]:
    entries = []
    for line in open(path):
        text = line.split("#", 1)[0].strip()
        if text:
            s, d = (int(p, 0) for p in text.split())
            entries.append((s, d))
    return entries


def model_lookup(entries, status: int) -> int:
    """Mirror of anem_status_to_dos_error() in statusdos.c."""
    if status & CUSTOMER:
        return status
    if (status & WIN32_MASK) == WIN32_HI:
        return status & 0xFFFF
    s = status
    if (s & HRESULT_MASK) == HRESULT_HI:
        s &= HRESULT_CLEAR
    lo, hi = 0, len(entries)
    while lo < hi:
        mid = (lo + hi) // 2
        if entries[mid][0] < s:
            lo = mid + 1
        else:
            hi = mid
    if lo < len(entries) and entries[lo][0] == s:
        return entries[lo][1]
    if (s >> 16) == OS2_HI:
        return s & 0xFFFF
    return DEFAULT


def read_cert(path: str, entries) -> list[tuple[int, int]]:
    """The (status, xp) corrections, or an exception the caller reports."""
    data = open(path, "rb").read()
    if len(data) < HDR_SIZE + FTR_SIZE:
        raise SystemExit(f"refusing: {path} is too small to be a certificate")
    hdr = data[:HDR_SIZE]
    if hdr[:4] != HDR_MAGIC:
        raise SystemExit(f"refusing: bad header magic {hdr[:4]!r}")
    version, entry_count, crc = struct.unpack_from("<III", hdr, 4)
    blob = b"".join(struct.pack("<II", s, d) for s, d in entries)
    want = zlib.crc32(blob) & 0xFFFFFFFF
    if version != 1 or entry_count != len(entries) or crc != want:
        raise SystemExit(
            f"refusing: stale certificate (judged table {entry_count}/"
            f"{crc:08x}, current {len(entries)}/{want:08x})")
    ftr = data[-FTR_SIZE:]
    if ftr[:4] != FTR_MAGIC:
        raise SystemExit("refusing: no footer — the probe never finished")
    total = struct.unpack_from("<Q", ftr, 4)[0]
    mask = struct.unpack_from("<I", ftr, 44)[0]
    checked = struct.unpack_from("<Q", ftr, 48)[0]
    if checked != 1 << 32:
        raise SystemExit(f"refusing: walk covered {checked} statuses, not 2^32")
    if mask:
        raise SystemExit(
            f"refusing: quadrant mask {mask:#x} — capped divergence is a "
            "wrong rule, not missing entries; fix statusdos.c's rules first")
    body = len(data) - HDR_SIZE - FTR_SIZE
    if body < 0 or body % REC_SIZE or body // REC_SIZE != total:
        raise SystemExit("refusing: record count disagrees with footer total")
    out = []
    prev = -1
    for i in range(total):
        status, ours, xp = struct.unpack_from(
            "<III", data, HDR_SIZE + i * REC_SIZE)
        if status <= prev:
            raise SystemExit(f"refusing: records not ascending at 0x{status:08x}")
        prev = status
        if ours != model_lookup(entries, status):
            raise SystemExit(
                f"refusing: 0x{status:08x} record says the model answered "
                f"{ours}, current model answers {model_lookup(entries, status)}")
        # Rule 3 rewrites 0xD... to 0xC... before the table is searched, so a
        # correction learned from such a status belongs on the rewritten key:
        # an entry under the 0xD... form could never be consulted, and
        # gen_model.py would refuse it. The two records of one masked status
        # must agree; XP cannot answer differently for the same lookup.
        key = unmask(status)
        if out and out[-1][0] == key:
            if out[-1][1] != xp:
                raise SystemExit(
                    f"refusing: 0x{status:08x} and the status before it both "
                    f"unmask to 0x{key:08x} but XP answered {out[-1][1]} and "
                    f"{xp}: XP cannot disagree with itself there")
            continue
        out.append((key, xp))
    return out


def build_new_text(path: str, entries, corrections) -> tuple[str, int]:
    """(new file text, statuses whose answer changes). Pure — writing is the
    caller's decision, which is what makes the dry run a real dry run."""
    fix = dict(corrections)
    old = dict(entries)
    changed = sum(1 for s, xp in corrections
                  if old.get(s, model_lookup(entries, s)) != xp)

    # New entries, sorted: they are inserted at their position among the
    # data lines, never appended — model.txt must stay ascending.
    present = set(old)
    pending = sorted((s, fix[s]) for s in fix
                     if s not in present and fix[s] != rule_output(s))

    out = []
    prev_dropped = False
    for line in open(path):
        text = line.split("#", 1)[0].strip()
        if not text:
            # An indented comment after a dropped entry is its continuation
            # and goes with it; a standalone comment stands on its own.
            if prev_dropped and line[:1].isspace():
                continue
            out.append(line)
            continue
        prev_dropped = False
        status = int(text.split()[0], 0)
        while pending and pending[0][0] < status:
            s, d = pending.pop(0)
            out.append(f"0x{s:08X} 0x{d:08X}{NEW_COMMENT}\n")
        if status in fix:
            xp = fix[status]
            if xp == rule_output(status):
                prev_dropped = True      # the rule says it: no entry needed
                continue
            comment = line[len(text):].rstrip("\n")
            out.append(f"0x{status:08X} 0x{xp:08X}{comment}\n")
        else:
            out.append(line)
    for s, d in pending:
        out.append(f"0x{s:08X} 0x{d:08X}{NEW_COMMENT}\n")
    return "".join(out), changed


def main() -> int:
    args = [a for a in sys.argv[1:] if a != "--write"]
    write = "--write" in sys.argv[1:]
    if len(args) != 2:
        print(__doc__)
        return 2
    model_path, cert_path = args
    entries = parse_model(model_path)
    corrections = read_cert(cert_path, entries)
    if not corrections:
        print("nothing to learn: the certificate reports zero mismatches")
        return 0
    text, changed = build_new_text(model_path, entries, corrections)
    verb = "rewrote" if write else "would rewrite"
    print(f"{verb} {model_path}: {changed} entry change(s) from "
          f"{len(corrections)} mismatches ({len(entries)} entries)")
    if write:
        with open(model_path, "w") as f:
            f.write(text)
    else:
        print("dry run: pass --write to apply, then re-run gen_model.py")
    return 0


if __name__ == "__main__":
    sys.exit(main())
