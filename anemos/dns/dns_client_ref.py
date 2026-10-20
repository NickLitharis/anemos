#!/usr/bin/env python3
"""Independent model of the DNS client, plus a query/response corpus.

Written from RFC 1035 and the behaviour documented in dns_client.h. The
differential gate feeds each corpus case (a query name/type and a response) to
both this model and the C client, and requires identical results.

usage:
  python3 dns_client_ref.py --check      self-check the built-in cases
  python3 dns_client_ref.py --emit DIR   write corpus.bin + expected.txt
"""

from __future__ import annotations

import struct
import sys
from pathlib import Path

import dns_ref

# Mirrors AnemDnsClientStatus.
OK = 0
BADARG = 1
BADSTATE = 2
IGNORED = 3
MALFORMED = 4
TRUNCATED = 5
RCODE = 6
NO_ANSWER = 7
CNAME = 8
LOOP = 9
NOSPACE = 10

TYPE_A = 1
TYPE_CNAME = 5
TYPE_AAAA = 28
CLASS_IN = 1

FLAG_QR = 0x8000
FLAG_TC = 0x0200
FLAG_RD = 0x0100
FLAG_RA = 0x0080

SEED = 1
MAX_ANSWERS = 32


def enc_name(text: str) -> bytes:
    if text == ".":
        return b"\x00"
    out = bytearray()
    for label in text.rstrip(".").split("."):
        b = label.encode("ascii")
        out.append(len(b))
        out += b
    out.append(0)
    return bytes(out)


def mk_response(qname: str, qtype: int, ident: int, flags: int,
                answers: list[tuple[str, int, int, bytes]]) -> bytes:
    out = bytearray(struct.pack(">HHHHHH", ident, flags, 1, len(answers), 0, 0))
    out += enc_name(qname)
    out += struct.pack(">HH", qtype, CLASS_IN)
    for owner, rtype, ttl, rdata in answers:
        out += enc_name(owner)
        out += struct.pack(">HHIH", rtype, CLASS_IN, ttl, len(rdata))
        out += rdata
    return bytes(out)


def name_equal_ci(a: bytes, b: bytes) -> bool:
    if len(a) != len(b):
        return False
    return all((x + 32 if 65 <= x <= 90 else x) == (y + 32 if 65 <= y <= 90 else y)
               for x, y in zip(a, b))


class Client:
    def __init__(self, seed: int) -> None:
        self.seed = seed
        self.id = 0
        self.qtype = 0
        self.qclass = CLASS_IN
        self.qname = b""
        self.pending = False

    def query(self, qname: bytes, qtype: int, qclass: int = CLASS_IN) -> int:
        if qtype not in (TYPE_A, TYPE_AAAA):
            return BADARG
        if self.pending:
            return BADSTATE
        self.seed = (self.seed * 1664525 + 1013904223) & 0xFFFFFFFF
        self.id = self.seed & 0xFFFF
        self.qname = qname
        self.qtype = qtype
        self.qclass = qclass
        self.pending = True
        return OK

    def receive(self, wire: bytes) -> tuple[int, dict]:
        result = {"codec_status": 0, "rcode": 0, "family": 0,
                  "addr": b"", "ttl": 0, "name": b""}
        if not self.pending:
            return BADSTATE, result
        try:
            msg = dns_ref.parse(wire)
        except dns_ref.DnsError as exc:
            result["codec_status"] = exc.code
            return MALFORMED, result
        if (msg.header.flags & FLAG_QR) == 0 or msg.header.id != self.id \
                or msg.header.qdcount != 1:
            return IGNORED, result
        q = msg.questions[0]
        qname, _ = dns_ref.read_name(msg.wire, q.name_off)
        if not name_equal_ci(qname, self.qname) or q.type != self.qtype \
                or q.class_ != self.qclass:
            return IGNORED, result

        self.pending = False
        if msg.header.flags & FLAG_TC:
            return TRUNCATED, result
        rcode = msg.header.flags & 0xF
        if rcode != 0:
            result["rcode"] = rcode
            return RCODE, result
        return self.walk_answers(msg, result)

    def walk_answers(self, msg: dns_ref.Message, result: dict) -> tuple[int, dict]:
        current = self.qname
        need = 4 if self.qtype == TYPE_A else 16
        for step in range(MAX_ANSWERS + 1):
            followed = False
            for rr in msg.answers:
                if rr.type != TYPE_CNAME:
                    continue
                owner, _ = dns_ref.read_name(msg.wire, rr.name_off)
                if not name_equal_ci(owner, current):
                    continue
                target, _ = dns_ref.read_name(msg.wire, rr.rdata_off)
                current = target
                followed = True
                break
            if followed:
                if step >= msg.header.ancount:
                    result["name"] = current
                    return LOOP, result
                continue
            for rr in msg.answers:
                if rr.type != self.qtype or rr.rdlength != need:
                    continue
                owner, _ = dns_ref.read_name(msg.wire, rr.name_off)
                if not name_equal_ci(owner, current):
                    continue
                result["addr"] = msg.wire[rr.rdata_off : rr.rdata_off + need]
                result["family"] = 4 if need == 4 else 6
                result["ttl"] = rr.ttl
                result["name"] = current
                return OK, result
            break
        result["name"] = current
        if not name_equal_ci(current, self.qname):
            return CNAME, result
        return NO_ANSWER, result


def case_result(qname: bytes, qtype: int, response: bytes,
                seed: int = SEED) -> tuple[int, dict]:
    cl = Client(seed)
    st = cl.query(qname, qtype)
    if st != OK:
        return st, {"codec_status": 0, "rcode": 0, "family": 0,
                    "addr": b"", "ttl": 0, "name": b""}
    return cl.receive(response)


def case_line(index: int, qname: bytes, qtype: int, response: bytes) -> str:
    st, r = case_result(qname, qtype, response)
    addr = r["addr"].hex() if r["addr"] else "-"
    name = r["name"].hex() if r["name"] else "-"
    return (f"{index} {st} {r['codec_status']} {r['rcode']} {r['family']} "
            f"{addr} {r['ttl']} {name}")


# ---- corpus ---------------------------------------------------------------

A_ADDR = bytes((192, 0, 2, 1))
AAAA_ADDR = bytes.fromhex("20010db8000000000000000000000001")


def id_for_seed(seed: int) -> int:
    return ((seed * 1664525 + 1013904223) & 0xFFFFFFFF) & 0xFFFF


def base_cases() -> list[tuple[str, str, int, bytes]]:
    ident = id_for_seed(SEED)
    q = "www.example.com"
    qw = enc_name(q)
    cases: list[tuple[str, str, int, bytes]] = []

    def add(name: str, qname: str, qtype: int, flags: int,
            answers: list[tuple[str, int, int, bytes]],
            resp_id: int | None = None,
            resp_qname: str | None = None) -> None:
        wire = mk_response(qname if resp_qname is None else resp_qname,
                           qtype, ident if resp_id is None else resp_id,
                           flags, answers)
        cases.append((name, enc_name(qname).hex(), qtype, wire))

    add("a", q, TYPE_A, FLAG_QR | FLAG_RD | FLAG_RA,
        [(q, TYPE_A, 300, A_ADDR)])
    add("aaaa", q, TYPE_AAAA, FLAG_QR | FLAG_RD | FLAG_RA,
        [(q, TYPE_AAAA, 60, AAAA_ADDR)])
    add("cname_chain", q, TYPE_A, FLAG_QR | FLAG_RD | FLAG_RA,
        [(q, TYPE_CNAME, 60, enc_name("alias.example.com")),
         ("alias.example.com", TYPE_A, 120, A_ADDR)])
    add("cname_only", q, TYPE_A, FLAG_QR | FLAG_RD | FLAG_RA,
        [(q, TYPE_CNAME, 60, enc_name("alias.example.com"))])
    add("cname_loop", "a.example.com", TYPE_A, FLAG_QR | FLAG_RD | FLAG_RA,
        [("a.example.com", TYPE_CNAME, 60, enc_name("b.example.com")),
         ("b.example.com", TYPE_CNAME, 60, enc_name("a.example.com"))])
    add("nxdomain", q, TYPE_A, FLAG_QR | FLAG_RD | FLAG_RA | 3, [])
    add("servfail", q, TYPE_A, FLAG_QR | FLAG_RD | FLAG_RA | 2, [])
    add("refused", q, TYPE_A, FLAG_QR | FLAG_RD | FLAG_RA | 5, [])
    add("truncated", q, TYPE_A, FLAG_QR | FLAG_RD | FLAG_RA | FLAG_TC, [])
    add("no_answer", q, TYPE_A, FLAG_QR | FLAG_RD | FLAG_RA, [])
    add("wrong_id", q, TYPE_A, FLAG_QR | FLAG_RD | FLAG_RA,
        [(q, TYPE_A, 300, A_ADDR)], resp_id=(ident ^ 0xFFFF))
    add("wrong_question", q, TYPE_A, FLAG_QR | FLAG_RD | FLAG_RA,
        [("other.example.com", TYPE_A, 300, A_ADDR)],
        resp_qname="other.example.com")
    add("not_a_response", q, TYPE_A, FLAG_RD | FLAG_RA,
        [(q, TYPE_A, 300, A_ADDR)])
    add("case_echo", q, TYPE_A, FLAG_QR | FLAG_RD | FLAG_RA,
        [("WWW.Example.COM", TYPE_A, 300, A_ADDR)])
    add("short_a_rdata", q, TYPE_A, FLAG_QR | FLAG_RD | FLAG_RA,
        [(q, TYPE_A, 300, A_ADDR[:3])])
    return cases


def make_corpus() -> list[tuple[str, str, int, bytes]]:
    cases = base_cases()
    valid = list(cases)
    # Truncations and mutations of every valid response.
    rng = 0x515EED

    def nxt() -> int:
        nonlocal rng
        rng = (rng * 1103515245 + 12345) & 0x7FFFFFFF
        return rng

    extra: list[tuple[str, str, int, bytes]] = []
    for name, qhex, qtype, wire in valid:
        for n in range(len(wire) + 1):
            extra.append((f"{name}_trunc{n}", qhex, qtype, wire[:n]))
        for k in range(48):
            idx = nxt() % len(wire)
            val = nxt() & 0xFF
            mutated = bytearray(wire)
            mutated[idx] = val
            extra.append((f"{name}_mut{k}", qhex, qtype, bytes(mutated)))
        for k in range(12):
            idx = nxt() % len(wire)
            val = nxt() & 0xFF
            extra.append((f"{name}_ins{k}", qhex, qtype,
                          wire[:idx] + bytes((val,)) + wire[idx:]))
            extra.append((f"{name}_del{k}", qhex, qtype,
                          wire[:idx] + wire[idx + 1:]))
    return cases + extra


CASE_SEED = SEED


def emit(directory: Path) -> None:
    directory.mkdir(parents=True, exist_ok=True)
    cases = make_corpus()
    blob = bytearray()
    lines = []
    for i, (_, qhex, qtype, wire) in enumerate(cases):
        qname = bytes.fromhex(qhex)
        blob += struct.pack("<H", len(qname)) + qname
        blob += struct.pack("<H", qtype)
        blob += struct.pack("<I", len(wire)) + wire
        lines.append(case_line(i, qname, qtype, wire))
    (directory / "corpus.bin").write_bytes(bytes(blob))
    (directory / "expected.txt").write_text("\n".join(lines) + "\n",
                                            encoding="ascii")
    print(f"corpus: {len(cases)} cases, {len(blob)} bytes")


def check() -> int:
    failures = 0
    for name, qhex, qtype, wire in base_cases():
        st, r = case_result(bytes.fromhex(qhex), qtype, wire)
        if st == OK and not r["addr"]:
            print(f"FAIL {name}: OK without an address")
            failures += 1
        elif st == CNAME and not r["name"]:
            print(f"FAIL {name}: CNAME without a target")
            failures += 1
        else:
            print(f"PASS {name} (status {st})")
    # A response that is not a response must leave the query pending.
    cl = Client(SEED)
    cl.query(enc_name("www.example.com"), TYPE_A)
    st, _ = cl.receive(b"\x00" * 5)
    if st != MALFORMED or not cl.pending:
        print("FAIL malformed frame consumed the transaction")
        failures += 1
    else:
        print("PASS malformed frame leaves the query pending")
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
