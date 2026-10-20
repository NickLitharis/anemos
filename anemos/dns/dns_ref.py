#!/usr/bin/env python3
"""Independent reference for anemos/dns — the second implementation.

The C codec is not allowed to be its own oracle. This module implements the
RFC 1035 wire format a second time, from the RFC, in Python:

  * classify(wire)  -> the same status vocabulary as AnemDnsStatus
  * parse(wire)     -> a message model
  * write(msg)      -> canonical bytes, using the same deterministic
                       longest-suffix compression rule as dns.c
  * make_corpus()   -> valid, malformed and mutated messages

The differential gate (`run.sh`) feeds the corpus to a native build of the C
codec and requires identical status codes and identical canonical bytes for
every case. Agreement between two independent implementations is the evidence;
either one alone proves nothing.

usage:
  python3 dns_ref.py --check          self-check the golden vectors
  python3 dns_ref.py --emit DIR       write corpus.bin and expected.txt
"""

from __future__ import annotations

import struct
import sys
from dataclasses import dataclass, field
from pathlib import Path

# Mirrors AnemDnsStatus in dns.h. The numeric values are the wire format of
# the differential test; keep them in the same order as the C enum.
OK = 0
BADARG = 1
TOO_LONG = 2
SHORT = 3
TRUNCATED = 4
BAD_LABEL = 5
BAD_POINTER = 6
BAD_RDLENGTH = 7
TOO_MANY = 8
TRAILING = 9
NOSPACE = 10
FORMAT = 11
NO_NAME = 12

MAX_WIRE = 65535
MAX_NAME = 255
MAX_LABEL = 63
MAX_HOPS = 256
MAX_Q = 4
MAX_AN = 32
MAX_NS = 8
MAX_AR = 8
MAX_COMPRESS = 512

TYPE_NS = 2
TYPE_CNAME = 5
TYPE_SOA = 6
TYPE_PTR = 12
TYPE_MX = 15
TYPE_SRV = 33


class DnsError(Exception):
    def __init__(self, code: int) -> None:
        super().__init__(str(code))
        self.code = code


@dataclass
class Header:
    id: int = 0
    flags: int = 0
    qdcount: int = 0
    ancount: int = 0
    nscount: int = 0
    arcount: int = 0


@dataclass
class Question:
    name_off: int
    type: int
    class_: int


@dataclass
class Record:
    name_off: int
    type: int
    class_: int
    ttl: int
    rdata_off: int
    rdlength: int


@dataclass
class Message:
    wire: bytes
    header: Header = field(default_factory=Header)
    questions: list[Question] = field(default_factory=list)
    answers: list[Record] = field(default_factory=list)
    authority: list[Record] = field(default_factory=list)
    additional: list[Record] = field(default_factory=list)


def read_name(wire: bytes, off: int) -> tuple[bytes, int]:
    """Return (uncompressed wire name, offset just past the stored name)."""
    pos = off
    end = 0
    out = bytearray()
    hops = 0
    jumped = False

    while True:
        if pos >= len(wire):
            raise DnsError(TRUNCATED)
        b = wire[pos]
        if b == 0:
            if not jumped:
                end = pos + 1
            break
        if b & 0xC0 == 0xC0:
            if pos + 1 >= len(wire):
                raise DnsError(TRUNCATED)
            target = ((b & 0x3F) << 8) | wire[pos + 1]
            if not jumped:
                end = pos + 2
            jumped = True
            if target >= pos:
                raise DnsError(BAD_POINTER)
            hops += 1
            if hops > MAX_HOPS:
                raise DnsError(BAD_POINTER)
            pos = target
            continue
        if b & 0xC0:
            raise DnsError(BAD_LABEL)
        if pos + 1 + b > len(wire):
            raise DnsError(TRUNCATED)
        if len(out) + 1 + b + 1 > MAX_NAME:
            raise DnsError(BAD_LABEL)
        out += wire[pos : pos + 1 + b]
        pos += 1 + b

    out.append(0)
    return bytes(out), end


def check_one_name_rdata(wire: bytes, rdata_off: int, rdlength: int, fixed: int) -> None:
    if fixed > rdlength:
        raise DnsError(BAD_RDLENGTH)
    _, end = read_name(wire, rdata_off + fixed)
    if end != rdata_off + rdlength:
        raise DnsError(BAD_RDLENGTH)


def check_soa_rdata(wire: bytes, rdata_off: int, rdlength: int) -> None:
    _, c1 = read_name(wire, rdata_off)
    if c1 - rdata_off > rdlength:
        raise DnsError(BAD_RDLENGTH)
    _, c2 = read_name(wire, c1)
    if c2 - rdata_off > rdlength:
        raise DnsError(BAD_RDLENGTH)
    if rdlength - (c2 - rdata_off) != 20:
        raise DnsError(BAD_RDLENGTH)


def validate_rdata(wire: bytes, r: Record) -> None:
    if r.type in (TYPE_NS, TYPE_CNAME, TYPE_PTR):
        check_one_name_rdata(wire, r.rdata_off, r.rdlength, 0)
    elif r.type == TYPE_MX:
        check_one_name_rdata(wire, r.rdata_off, r.rdlength, 2)
    elif r.type == TYPE_SRV:
        check_one_name_rdata(wire, r.rdata_off, r.rdlength, 6)
    elif r.type == TYPE_SOA:
        check_soa_rdata(wire, r.rdata_off, r.rdlength)


def read_record(wire: bytes, off: int) -> tuple[Record, int]:
    if off >= len(wire):
        raise DnsError(TRUNCATED)
    name_off = off
    _, off = read_name(wire, off)
    if len(wire) - off < 10:
        raise DnsError(SHORT)
    rtype, rclass, ttl, rdlength = struct.unpack_from(">HHIH", wire, off)
    off += 10
    rdata_off = off
    if rdlength > len(wire) - off:
        raise DnsError(TRUNCATED)
    rec = Record(name_off, rtype, rclass, ttl, rdata_off, rdlength)
    validate_rdata(wire, rec)
    return rec, off + rdlength


def parse(wire: bytes) -> Message:
    if len(wire) > MAX_WIRE:
        raise DnsError(TOO_LONG)
    if len(wire) < 12:
        raise DnsError(SHORT)
    msg = Message(wire=bytes(wire))
    (msg.header.id, msg.header.flags, msg.header.qdcount, msg.header.ancount,
     msg.header.nscount, msg.header.arcount) = struct.unpack_from(">HHHHHH", wire, 0)
    if (msg.header.qdcount > MAX_Q or msg.header.ancount > MAX_AN
            or msg.header.nscount > MAX_NS or msg.header.arcount > MAX_AR):
        raise DnsError(TOO_MANY)

    off = 12
    for _ in range(msg.header.qdcount):
        if off >= len(wire):
            raise DnsError(TRUNCATED)
        name_off = off
        _, off = read_name(wire, off)
        if len(wire) - off < 4:
            raise DnsError(SHORT)
        qtype, qclass = struct.unpack_from(">HH", wire, off)
        off += 4
        msg.questions.append(Question(name_off, qtype, qclass))

    for count, dest in ((msg.header.ancount, msg.answers),
                        (msg.header.nscount, msg.authority),
                        (msg.header.arcount, msg.additional)):
        for _ in range(count):
            rec, off = read_record(wire, off)
            dest.append(rec)

    if off != len(wire):
        raise DnsError(TRAILING)
    return msg


def classify(wire: bytes) -> int:
    try:
        parse(bytes(wire))
        return OK
    except DnsError as exc:
        return exc.code


# ---- canonical writer -----------------------------------------------------

class Writer:
    def __init__(self, cap: int = MAX_WIRE) -> None:
        self.buf = bytearray()
        self.cap = cap
        self.labels: list[int] = []

    def bytes(self, data: bytes) -> None:
        if len(self.buf) + len(data) > self.cap:
            raise DnsError(NOSPACE)
        self.buf += data

    def u8(self, v: int) -> None:
        self.bytes(bytes((v & 0xFF,)))

    def u16(self, v: int) -> None:
        self.bytes(struct.pack(">H", v))

    def u32(self, v: int) -> None:
        self.bytes(struct.pack(">I", v))

    def label(self, name: bytes, p: int) -> None:
        if len(self.buf) < 0x4000 and len(self.labels) < MAX_COMPRESS:
            self.labels.append(len(self.buf))
        self.bytes(name[p : p + 1 + name[p]])

    def name(self, name: bytes) -> None:
        eligible = len(self.labels)
        best_s = None
        best_c = 0
        s = 0
        while True:
            suffix_len = len(name) - s
            if suffix_len > 2:
                for k in range(eligible):
                    c = self.labels[k]
                    if c >= 0x4000:
                        continue
                    if out_name_equals(bytes(self.buf), c, name[s:]):
                        if best_s is None or s < best_s or (s == best_s and c < best_c):
                            best_s = s
                            best_c = c
                        break
            if name[s] == 0:
                break
            s += 1 + name[s]

        p = 0
        if best_s is not None:
            while p < best_s:
                self.label(name, p)
                p += 1 + name[p]
            self.u8(0xC0 | (best_c >> 8))
            self.u8(best_c & 0xFF)
        else:
            while name[p] != 0:
                self.label(name, p)
                p += 1 + name[p]
            self.u8(0)


def out_name_equals(out: bytes, cand: int, name: bytes) -> bool:
    p = cand
    i = 0
    total = 0
    hops = 0
    while True:
        if p >= len(out) or i >= len(name):
            return False
        b = out[p]
        if b & 0xC0 == 0xC0:
            if p + 1 >= len(out):
                return False
            target = ((b & 0x3F) << 8) | out[p + 1]
            if target >= p:
                return False
            hops += 1
            if hops > MAX_HOPS:
                return False
            p = target
            continue
        if b == 0:
            return name[i] == 0 and i + 1 == len(name)
        if b & 0xC0 or b != name[i]:
            return False
        if p + 1 + b > len(out) or i + 1 + b > len(name):
            return False
        if out[p + 1 : p + 1 + b] != name[i + 1 : i + 1 + b]:
            return False
        total += b + 1
        if total + 1 > MAX_NAME:
            return False
        p += 1 + b
        i += 1 + b


def check_name(name: bytes) -> None:
    if not name or len(name) > MAX_NAME:
        raise DnsError(BAD_LABEL)
    p = 0
    while True:
        if p >= len(name):
            raise DnsError(BAD_LABEL)
        b = name[p]
        if b == 0:
            if p + 1 != len(name):
                raise DnsError(BAD_LABEL)
            return
        if b & 0xC0 or b > MAX_LABEL:
            raise DnsError(BAD_LABEL)
        if p + 1 + b >= len(name):
            raise DnsError(BAD_LABEL)
        p += 1 + b


def w_name(w: Writer, name: bytes) -> None:
    check_name(name)
    w.name(name)


def w_question(w: Writer, msg: Message, q: Question) -> None:
    name, _ = read_name(msg.wire, q.name_off)
    w_name(w, name)
    w.u16(q.type)
    w.u16(q.class_)


def w_soa_rdata(w: Writer, msg: Message, r: Record) -> None:
    mname, c1 = read_name(msg.wire, r.rdata_off)
    rname, c2 = read_name(msg.wire, c1)
    if c2 > len(msg.wire) or 20 > len(msg.wire) - c2:
        raise DnsError(FORMAT)
    if c2 - r.rdata_off + 20 != r.rdlength:
        raise DnsError(FORMAT)
    w_name(w, mname)
    w_name(w, rname)
    w.bytes(msg.wire[c2 : c2 + 20])


def w_rdata(w: Writer, msg: Message, r: Record) -> None:
    if r.rdata_off > len(msg.wire) or r.rdlength > len(msg.wire) - r.rdata_off:
        raise DnsError(FORMAT)
    if r.type in (TYPE_NS, TYPE_CNAME, TYPE_PTR):
        fixed = 0
    elif r.type == TYPE_MX:
        fixed = 2
    elif r.type == TYPE_SRV:
        fixed = 6
    elif r.type == TYPE_SOA:
        w_soa_rdata(w, msg, r)
        return
    else:
        w.bytes(msg.wire[r.rdata_off : r.rdata_off + r.rdlength])
        return
    if fixed > r.rdlength:
        raise DnsError(FORMAT)
    w.bytes(msg.wire[r.rdata_off : r.rdata_off + fixed])
    name, _ = read_name(msg.wire, r.rdata_off + fixed)
    w_name(w, name)


def w_record(w: Writer, msg: Message, r: Record) -> None:
    if r.rdata_off > len(msg.wire) or r.rdlength > len(msg.wire) - r.rdata_off:
        raise DnsError(FORMAT)
    owner, _ = read_name(msg.wire, r.name_off)
    w_name(w, owner)
    w.u16(r.type)
    w.u16(r.class_)
    w.u32(r.ttl)
    len_pos = len(w.buf)
    w.u16(0)
    start = len(w.buf)
    w_rdata(w, msg, r)
    rdlen = len(w.buf) - start
    if rdlen > 0xFFFF:
        raise DnsError(FORMAT)
    w.buf[len_pos] = rdlen >> 8
    w.buf[len_pos + 1] = rdlen & 0xFF


def write(msg: Message, cap: int = MAX_WIRE) -> bytes:
    w = Writer(cap)
    h = msg.header
    if h.qdcount > MAX_Q or h.ancount > MAX_AN or h.nscount > MAX_NS or h.arcount > MAX_AR:
        raise DnsError(BADARG)
    w.u16(h.id)
    w.u16(h.flags)
    w.u16(h.qdcount)
    w.u16(h.ancount)
    w.u16(h.nscount)
    w.u16(h.arcount)
    for q in msg.questions[: h.qdcount]:
        w_question(w, msg, q)
    for r in msg.answers[: h.ancount]:
        w_record(w, msg, r)
    for r in msg.authority[: h.nscount]:
        w_record(w, msg, r)
    for r in msg.additional[: h.arcount]:
        w_record(w, msg, r)
    return bytes(w.buf)


def canon(wire: bytes, cap: int = MAX_WIRE) -> bytes:
    return write(parse(wire), cap)


def fnv1a(data: bytes) -> int:
    h = 2166136261
    for b in data:
        h = ((h ^ b) * 16777619) & 0xFFFFFFFF
    return h


# ---- golden vectors -------------------------------------------------------
#
# Hand-authored from RFC 1035, not produced by the encoder above. They are
# the independent anchor: if both implementations drifted the same way, these
# literals would still disagree with them.

GOLDEN: list[tuple[str, str]] = [
    # Plain recursive query, www.example.com IN A.
    ("query_a",
     "123401000001000000000000"
     "03777777" "076578616d706c65" "03636f6d" "00" "0001" "0001"),
    # Response with CNAME (alias.example.com) then A for the alias. Offsets:
    # www=12, example=16, com=24; answer1 rdata name at 45.
    ("cname_then_a",
     "abcd81800001000200000000"
     "03777777" "076578616d706c65" "03636f6d" "00" "0001" "0001"
     "c00c" "0005" "0001" "0000012c" "0008" "05616c696173" "c010"
     "c02d" "0001" "0001" "0000012c" "0004" "c0000201"),
    # MX response: preference then a compressed name. mail=12, example=17,
    # com=25; RDATA is 000a + pointer to example.com.
    ("mx",
     "000181800001000100000000"
     "046d61696c" "076578616d706c65" "03636f6d" "00" "000f" "0001"
     "c00c" "000f" "0001" "00000e10" "0004" "000a" "c011"),
    # SOA in the authority section: MNAME and RNAME both compress against
    # the question's example.com.
    ("soa_authority",
     "000281800001000000010000"
     "03777777" "076578616d706c65" "03636f6d" "00" "0001" "0001"
     "c00c" "0006" "0001" "0000012c" "0026"
     "026e73" "c010"
     "0a686f73746d6173746572" "c010"
     "00000001" "00000e10" "00000708" "00000e10" "0000012c"),
    # Root name query.
    ("root_ns",
     "000301000001000000000000" "00" "0002" "0001"),
    # SRV: priority, weight, port, then the target. _help=12, _tcp=18,
    # example=23, com=31; the RDATA target compresses to example.com.
    ("srv",
     "000481800001000100000000"
     "055f68656c70" "045f746370" "076578616d706c65" "03636f6d" "00"
     "0021" "0001"
     "c00c" "0021" "0001" "0000012c" "0008"
     "000a" "0014" "001f" "c017"),
    # A query for a name with nothing in common with anything else.
    ("query_single_label",
     "beef01000001000000000000" "0161" "00" "0001" "0001"),
]

MALFORMED: list[tuple[str, int]] = [
    ("empty", SHORT),
    ("header_11", SHORT),
    ("question_missing", TRUNCATED),
    ("reserved_label_bits", BAD_LABEL),
    ("self_pointer", BAD_POINTER),
    ("forward_pointer", BAD_POINTER),
    ("too_many_questions", TOO_MANY),
    ("trailing_byte", TRAILING),
    ("rdlength_past_end", TRUNCATED),
    ("cname_rdlength_short", BAD_RDLENGTH),
    ("question_type_missing", SHORT),
    ("name_over_255", BAD_LABEL),
]


def malformed_vectors() -> list[tuple[str, bytes, int]]:
    header_q1 = bytes.fromhex("000101000001000000000000")
    valid_q = bytes.fromhex("000101000001000000000000" "0161" "00" "0001" "0001")
    long_name = b""
    for _ in range(4):
        long_name += bytes((63,)) + b"a" * 63
    long_name += b"\x00"
    return [
        ("empty", b"", SHORT),
        ("header_11", b"\x00" * 11, SHORT),
        ("question_missing", header_q1, TRUNCATED),
        ("reserved_label_bits", header_q1 + b"\x40", BAD_LABEL),
        ("self_pointer", header_q1 + bytes.fromhex("c00c"), BAD_POINTER),
        ("forward_pointer", header_q1 + bytes.fromhex("c020"), BAD_POINTER),
        ("too_many_questions",
         bytes.fromhex("000101000005000000000000") + b"\x00" * 4, TOO_MANY),
        ("trailing_byte", valid_q + b"\x00", TRAILING),
        # one answer whose rdlength claims more bytes than exist
        ("rdlength_past_end",
         bytes.fromhex("000181800001000100000000" "0161" "00" "0001" "0001"
                       "c00c" "0001" "0001" "00000000" "00ff"), TRUNCATED),
        # CNAME whose name fills fewer bytes than rdlength says
        ("cname_rdlength_short",
         bytes.fromhex("000181800001000100000000" "0161" "00" "0001" "0001"
                       "c00c" "0005" "0001" "00000000" "0002" "0161" "00"), BAD_RDLENGTH),
        ("question_type_missing",
         bytes.fromhex("000101000001000000000000" "0161" "00" "0001"), SHORT),
        ("name_over_255", header_q1 + long_name, BAD_LABEL),
    ]


# ---- differential corpus --------------------------------------------------

class Lcg:
    def __init__(self, seed: int) -> None:
        self.x = seed & 0x7FFFFFFF

    def next(self) -> int:
        self.x = (1103515245 * self.x + 12345) & 0x7FFFFFFF
        return self.x


def make_corpus() -> list[bytes]:
    cases: list[bytes] = []
    valid: list[bytes] = []

    for _, hexstr in GOLDEN:
        wire = bytes.fromhex(hexstr)
        valid.append(wire)
        cases.append(wire)

    for _, wire, _ in malformed_vectors():
        cases.append(wire)

    # Truncations of every valid message at every prefix (cheap, exhaustive).
    for wire in valid:
        for n in range(len(wire) + 1):
            cases.append(wire[:n])

    # Deterministic single-byte mutations, insertions and deletions.
    rng = Lcg(0x5EED)
    for wire in valid:
        if not wire:
            continue
        for _ in range(96):
            idx = rng.next() % len(wire)
            val = rng.next() & 0xFF
            mutated = bytearray(wire)
            mutated[idx] = val
            cases.append(bytes(mutated))
        for _ in range(24):
            idx = rng.next() % len(wire)
            val = rng.next() & 0xFF
            cases.append(wire[:idx] + bytes((val,)) + wire[idx:])
            if wire:
                cases.append(wire[:idx] + wire[idx + 1:])
    return cases


def expectation(wire: bytes) -> str:
    code = classify(wire)
    if code != OK:
        return f"{code} -1 00000000 -1"
    try:
        canonical = canon(wire)
    except DnsError as exc:
        return f"{code} {exc.code} 00000000 -1"
    again = classify(canonical)
    return f"{code} {OK} {fnv1a(canonical):08x} {again}"


def emit(directory: Path) -> None:
    directory.mkdir(parents=True, exist_ok=True)
    cases = make_corpus()
    blob = bytearray()
    lines = []
    for i, wire in enumerate(cases):
        blob += struct.pack("<I", len(wire)) + wire
        lines.append(f"{i} {expectation(wire)}")
    (directory / "corpus.bin").write_bytes(bytes(blob))
    (directory / "expected.txt").write_text("\n".join(lines) + "\n", encoding="ascii")
    print(f"corpus: {len(cases)} cases, {len(blob)} bytes")


def check() -> int:
    failures = 0
    for name, hexstr in GOLDEN:
        wire = bytes.fromhex(hexstr)
        try:
            parsed = parse(wire)
        except DnsError as exc:
            print(f"FAIL {name}: parse rejected the golden vector ({exc.code})")
            failures += 1
            continue
        rewritten = write(parsed)
        if rewritten != wire:
            print(f"FAIL {name}: canonical write differs")
            print(f"  in  {wire.hex()}")
            print(f"  out {rewritten.hex()}")
            failures += 1
            continue
        again = parse(rewritten)
        if again.header != parsed.header:
            print(f"FAIL {name}: header changed on reparse")
            failures += 1
            continue
        print(f"PASS {name} ({len(wire)} bytes)")

    for name, wire, want in malformed_vectors():
        got = classify(wire)
        if got != want:
            print(f"FAIL {name}: classified {got}, want {want}")
            failures += 1
        else:
            print(f"PASS {name} (status {got})")

    # Every corpus case must be self-consistent: a successful parse must
    # canonicalise to bytes that parse again.
    for wire in make_corpus():
        code = classify(wire)
        if code == OK:
            try:
                canonical = canon(wire)
            except DnsError as exc:
                print(f"FAIL corpus: canonical write failed after parse ({exc.code})")
                failures += 1
                break
            if classify(canonical) != OK:
                print("FAIL corpus: canonical output does not parse")
                failures += 1
                break

    print(f"{failures} failure(s)")
    return 1 if failures else 0


def main(argv: list[str]) -> int:
    if len(argv) == 2 and argv[1] == "--check":
        return check()
    if len(argv) == 3 and argv[1] == "--emit":
        emit(Path(argv[2]))
        return 0
    print(__doc__)
    return 2


if __name__ == "__main__":
    sys.exit(main(sys.argv))
