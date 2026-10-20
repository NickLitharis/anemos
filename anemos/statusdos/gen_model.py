#!/usr/bin/env python3
"""Turn model.txt into table.c, or refuse to.

model.txt holds the STATUS->DOS model's exception table: the entries the
rules in statusdos.c (customer identity, Win32-error passthrough, HRESULT
unwrapping, redirector unwrapping, default 317) do not already produce. This
generator is the only thing allowed to write table.c, so the compiled table
can never drift from the model file the suite tests against.

Refusals, each of which means the model is wrong in a way a generated table
would hide forever:
  - duplicate status, or entries not strictly ascending;
  - an entry whose value the rules already produce (redundant: it can never
    change any lookup, so it is a modelling mistake, not a mapping);
  - an entry the rules intercept before the table is consulted (unreachable:
    customer bit set, 0x80070000..0x8007FFFF, or 0xD0000000..0xDFFFFFFF,
    which rule 3 rewrites before the search);
  - any of the 9 published anchors resolving to the wrong DOS error.

usage: gen_model.py <model.txt> <table.c>
prints the CRC-32 of the emitted entry array on the last line (also written
to <table.c>.crc): the in-guest probe embeds the same CRC in its certificate,
which is how a certificate from an older table is detected and refused.
"""
import struct
import sys
import zlib

# Published, independent of this model: documented Windows status -> error
# pairs. The in-guest probe re-verifies all nine against live XP, so these
# anchor the generator even before a certificate exists.
ANCHORS = {
    0x00000000: 0,      # STATUS_SUCCESS           -> ERROR_SUCCESS
    0xC000000D: 87,     # STATUS_INVALID_PARAMETER -> ERROR_INVALID_PARAMETER
    0xC0000005: 998,    # STATUS_ACCESS_VIOLATION  -> ERROR_NOACCESS
    0xC0000017: 8,      # STATUS_NO_MEMORY         -> ERROR_NOT_ENOUGH_MEMORY
    0xC0000022: 5,      # STATUS_ACCESS_DENIED     -> ERROR_ACCESS_DENIED
    0xC0000023: 122,    # STATUS_BUFFER_TOO_SMALL  -> ERROR_INSUFFICIENT_BUFFER
    0xC0000034: 2,      # STATUS_OBJECT_NAME_NOT_FOUND -> ERROR_FILE_NOT_FOUND
    0xC000003A: 3,      # STATUS_OBJECT_PATH_NOT_FOUND -> ERROR_PATH_NOT_FOUND
                        # (the first anchor list had 2 here; XP's own answer is 3)
    0xC000013A: 317,    # STATUS_CONTROL_C_EXIT -> NOT IN XP'S TABLE: it is
                        # ERROR_MR_MID_NOT_FOUND, not the documented 1223,
                        # which the in-guest walk proved. XP is the oracle.
}

DEFAULT = 317                     # ERROR_MR_MID_NOT_FOUND
CUSTOMER = 0x20000000             # rule 1: bit test, not a range
WIN32_HI, WIN32_MASK = 0x80070000, 0xFFFF0000      # rule 2
HRESULT_HI, HRESULT_MASK, HRESULT_CLEAR = 0xD0000000, 0xF0000000, 0xCFFFFFFF  # rule 3
OS2_HI = 0xC001                   # rule 5


def unmask(status: int) -> int:
    """The status the table is actually searched with: rule 3 clears the
    HRESULT bits, so an entry keyed on the 0xD... form is never consulted."""
    if (status & HRESULT_MASK) == HRESULT_HI:
        return status & HRESULT_CLEAR
    return status


def treated_as_win32(status: int) -> bool:
    return (status & WIN32_MASK) == WIN32_HI


def rule_output(status: int) -> int:
    """What statusdos.c answers when the table has no entry for `status`."""
    if status & CUSTOMER:
        return status
    if treated_as_win32(status):
        return status & 0xFFFF
    s = unmask(status)
    if (s >> 16) == OS2_HI:
        return s & 0xFFFF
    return DEFAULT


def unreachable(status: int) -> str | None:
    """Why the table can never be consulted at this status, or None."""
    if status & CUSTOMER:
        return "the customer bit makes rule 1 answer before the table"
    if treated_as_win32(status):
        return "rule 2 answers before the table"
    if unmask(status) != status:
        return (f"rule 3 rewrites it to 0x{unmask(status):08x} before the "
                "table is searched, so an entry here is dead weight")
    return None


def load(path: str) -> list[tuple[int, int]]:
    entries = []
    for lineno, line in enumerate(open(path), 1):
        text = line.split("#", 1)[0].strip()
        if not text:
            continue
        parts = text.split()
        if len(parts) != 2:
            raise SystemExit(f"{path}:{lineno}: want 'STATUS DOS', got {text!r}")
        try:
            status, dos = (int(p, 0) for p in parts)
        except ValueError:
            raise SystemExit(f"{path}:{lineno}: not two integers: {text!r}")
        if not 0 <= status <= 0xFFFFFFFF or not 0 <= dos <= 0xFFFFFFFF:
            raise SystemExit(f"{path}:{lineno}: value out of 32-bit range")
        entries.append((status, dos))
    if not entries:
        raise SystemExit(f"refusing: {path} has no entries")
    return entries


def validate(entries: list[tuple[int, int]]) -> None:
    prev = -1
    for status, dos in entries:
        if status <= prev:
            kind = "duplicate" if status == prev else "out of order"
            raise SystemExit(f"refusing: {kind} status 0x{status:08x}")
        prev = status
        dead = unreachable(status)
        if dead is not None:
            raise SystemExit(
                f"refusing: entry 0x{status:08x} is unreachable — {dead}")
        if dos == rule_output(status):
            raise SystemExit(
                f"refusing: entry 0x{status:08x} -> {dos} is redundant: the "
                "rules already produce it, so it can never change a lookup — "
                "either the rules are wrong or the entry is a mistake")
    for status, want in sorted(ANCHORS.items()):
        got = dict(entries).get(status, rule_output(status))
        if got != want:
            raise SystemExit(
                f"refusing: anchor 0x{status:08x} -> {got}, expected {want}"
                " (the model disagrees with published Windows behaviour)")


def emit(entries: list[tuple[int, int]], out_path: str) -> int:
    blob = b"".join(struct.pack("<II", s, d) for s, d in entries)
    crc = zlib.crc32(blob) & 0xFFFFFFFF
    with open(out_path, "w") as f:
        f.write("/* Generated by gen_model.py from model.txt — the XP guest's\n"
                " * own answers, plus published anchors. Do not edit; regenerate.\n"
                " * k_entries_crc32 fingerprints exactly these bytes; the in-guest\n"
                " * certificate carries the same CRC, which is how a certificate\n"
                " * from an older table is refused. */\n")
        f.write('#include "statusdos.c.h"\n\n')
        f.write(f"const uint32_t k_entries_crc32 = 0x{crc:08x}u;\n\n")
        f.write(f"const uint32_t k_entry_count = {len(entries)}u;\n\n")
        f.write("const struct AnemStatusDosEntry k_entries[] = {\n")
        for status, dos in entries:
            f.write(f"    {{ 0x{status:08x}u, 0x{dos:08x}u }},\n")
        f.write("};\n")
    with open(out_path + ".crc", "w") as f:
        f.write(f"{crc:08x}\n")
    return crc


def main() -> int:
    if len(sys.argv) != 3:
        print(__doc__)
        return 2
    entries = load(sys.argv[1])
    validate(entries)
    crc = emit(entries, sys.argv[2])
    print(f"ok: {len(entries)} entries, table {len(entries) * 8} bytes, "
          f"crc32 {crc:08x} -> {sys.argv[2]}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
