#!/usr/bin/env python3
"""Check an in-guest certificate (sdos-cert.bin) against the current table.

The certificate is what a model-verification run (mv_probe.c, inside the XP
guest) produces: a header naming the table it judged, only the mismatches,
and a footer proving the walk covered all 2^32 statuses. This checker turns
that file into one of two verdicts:

  VERIFIED  — zero mismatches: the shipping model answers exactly like XP's
              ntdll!RtlNtStatusToDosError for every one of the 2^32 statuses.
  DIVERGENT — mismatches exist; learn.py folds them into model.txt.

Every structural property is checked before either verdict, because a
truncated, corrupt, or stale certificate must never read as VERIFIED:
  - header magic "SDMV", version 1, and entry count + CRC matching the table
    generated from the *current* model.txt (a certificate from an older
    table is refused, not reconciled);
  - file size is exactly header + 12*nrecords + footer;
  - footer magic "SDMX", statuses-checked == 2^32, total == sum(quadrants);
  - records ascending by status, no duplicates, quadrant-consistent;
  - recorded-per-quadrant == footer total when that quadrant's cap bit is
    clear, and strictly less when it is set (capping must be visible);
  - every record's `ours` equals what the current model predicts for that
    status — belt and braces on top of the CRC, and it independently proves
    the certificate and the model disagree at exactly the recorded statuses.

usage: check_cert.py <model.txt> <sdos-cert.bin>     (exit 0 = VERIFIED)
"""
import struct
import sys

HDR_MAGIC = b"SDMV"
FTR_MAGIC = b"SDMX"
VERSION = 1
HDR_SIZE = 16
FTR_SIZE = 56
REC_SIZE = 12
QUAD_CAP = 0x40000          # must match QUAD_CAP in mv_probe.c

DEFAULT = 317
CUSTOMER = 0x20000000
WIN32_HI, WIN32_MASK = 0x80070000, 0xFFFF0000
HRESULT_HI, HRESULT_MASK, HRESULT_CLEAR = 0xD0000000, 0xF0000000, 0xCFFFFFFF
OS2_HI = 0xC001


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


def load_model(path: str) -> tuple[list[tuple[int, int]], int]:
    """(entries, crc32) — the same values gen_model.py stamps into table.c."""
    import zlib
    entries = []
    for line in open(path):
        text = line.split("#", 1)[0].strip()
        if text:
            s, d = (int(p, 0) for p in text.split())
            entries.append((s, d))
    blob = b"".join(struct.pack("<II", s, d) for s, d in entries)
    return entries, zlib.crc32(blob) & 0xFFFFFFFF


def model_lookup(entries: list[tuple[int, int]], status: int) -> int:
    """Mirror of anem_status_to_dos_error() in statusdos.c: rules first, in
    XP's order, the table with rule 3's mask applied."""
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


def fail(msg: str) -> int:
    print(f"REFUSED: {msg}")
    return 1


def main() -> int:
    if len(sys.argv) != 3:
        print(__doc__)
        return 2
    model_path, cert_path = sys.argv[1], sys.argv[2]

    entries, want_crc = load_model(model_path)
    data = open(cert_path, "rb").read()

    if len(data) < HDR_SIZE + FTR_SIZE:
        return fail(f"{cert_path} is {len(data)} bytes — smaller than a "
                    "header plus footer: the probe never finished writing")

    hdr = data[:HDR_SIZE]
    if hdr[:4] != HDR_MAGIC:
        return fail(f"header magic {hdr[:4]!r}, expected {HDR_MAGIC!r}")
    version, entry_count, crc = struct.unpack_from("<III", hdr, 4)
    if version != VERSION:
        return fail(f"certificate version {version}, checker handles {VERSION}")
    if entry_count != len(entries):
        return fail(f"certificate judged a {entry_count}-entry table, current "
                    f"model has {len(entries)}: stale certificate")
    if crc != want_crc:
        return fail(f"certificate table crc {crc:08x}, current model "
                    f"{want_crc:08x}: stale certificate")

    body = len(data) - HDR_SIZE - FTR_SIZE
    if body < 0 or body % REC_SIZE:
        return fail(f"body is {body} bytes, not a whole number of 12-byte "
                    "records: truncated or corrupt")
    nrec = body // REC_SIZE

    ftr = data[-FTR_SIZE:]
    if ftr[:4] != FTR_MAGIC:
        return fail(f"footer magic {ftr[:4]!r}, expected {FTR_MAGIC!r}: the "
                    "probe was cut off before it could finish")
    total, q0, q1, q2, q3 = struct.unpack_from("<5Q", ftr, 4)
    quad_tot = (q0, q1, q2, q3)
    mask, checked = struct.unpack_from("<IQ", ftr, 44)
    if checked != 1 << 32:
        return fail(f"footer claims {checked} statuses checked, not 2^32: "
                    "incomplete walk is not evidence")
    if total != sum(quad_tot):
        return fail(f"footer total {total} != sum of quadrants "
                    f"{sum(quad_tot)}")

    recs = [struct.unpack_from("<III", data, HDR_SIZE + i * REC_SIZE)
            for i in range(nrec)]
    prev = -1
    quad_rec = [0, 0, 0, 0]
    for status, ours, xp in recs:
        if status <= prev:
            return fail(f"records not strictly ascending at 0x{status:08x}")
        prev = status
        if ours == xp:
            return fail(f"record 0x{status:08x} has ours == xp "
                        f"({ours}): not a mismatch")
        quad_rec[status >> 30] += 1
        want = model_lookup(entries, status)
        if want != ours:
            return fail(f"record 0x{status:08x} says the model answered "
                        f"{ours}, but the current model answers {want}: the "
                        "certificate and the model disagree about the past")

    for q in range(4):
        if mask & (1 << q):
            # Capped means the probe wrote exactly QUAD_CAP records and the
            # true total ran past it. Anything else is an internally
            # inconsistent claim about what was dropped.
            if quad_rec[q] != QUAD_CAP:
                return fail(f"quadrant {q} is marked capped but recorded "
                            f"{quad_rec[q]} of the cap {QUAD_CAP}")
            if quad_tot[q] <= QUAD_CAP:
                return fail(f"quadrant {q} is marked capped but its total "
                            f"{quad_tot[q]} never reached the cap {QUAD_CAP}")
        elif quad_rec[q] != quad_tot[q]:
            return fail(f"quadrant {q} recorded {quad_rec[q]} of "
                        f"{quad_tot[q]} mismatches without being marked "
                        "capped: records were lost")

    if mask:
        qs = ",".join(str(q) for q in range(4) if mask & (1 << q))
        print(f"DIVERGENT: {total} mismatches, quadrant(s) {qs} hit the "
              f"record cap ({QUAD_CAP}) — that density is a wrong RULE, not "
              "missing table entries: widen statusdos.c's rules, then "
              "regenerate and re-run. First mismatches:")
        for status, ours, xp in recs[:12]:
            print(f"  0x{status:08x}: model {ours}, XP {xp}")
        return 1

    if total:
        print(f"DIVERGENT: {total} mismatches across the walk; recorded "
              f"{nrec}. Run learn.py to fold them into model.txt, then "
              "regenerate, rebuild the probe and re-run.")
        for status, ours, xp in recs[:12]:
            print(f"  0x{status:08x}: model {ours}, XP {xp}")
        if nrec > 12:
            print(f"  ... and {nrec - 12} more")
        return 1

    print(f"VERIFIED: the model matched XP for all 4294967296 statuses "
          f"({len(entries)} table entries, crc32 {want_crc:08x})")
    return 0


if __name__ == "__main__":
    sys.exit(main())
