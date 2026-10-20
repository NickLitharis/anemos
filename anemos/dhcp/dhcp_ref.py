#!/usr/bin/env python3
"""Independent reference for anemos/dhcp — the second implementation.

Written from RFC 2131 / RFC 2132, not from dhcp.c. It provides the same
classification vocabulary, a parser, an exact writer and a corpus generator.
The differential gate in run.sh requires the C codec and this module to agree
on every corpus case: status, canonical bytes and reparse status.

A stronger invariant also holds and is checked here: for every accepted
message, write(parse(w)) == w. The codec preserves interior PAD options and
zero padding after END, so the round-trip is exact rather than canonical.

usage:
  python3 dhcp_ref.py --check          self-check golden + malformed vectors
  python3 dhcp_ref.py --emit DIR       write corpus.bin and expected.txt
  python3 dhcp_ref.py --hex            print the golden wires as hex literals
"""

from __future__ import annotations

import struct
import sys
from dataclasses import dataclass, field
from pathlib import Path

# Mirrors AnemDhcpStatus in dhcp.h; keep in the same order.
OK = 0
BADARG = 1
TOO_LONG = 2
SHORT = 3
BAD_COOKIE = 4
BAD_HLEN = 5
BAD_OPTION = 6
NO_END = 7
TRAILING = 8
TOO_MANY = 9
NOSPACE = 10
FORMAT = 11
NOT_FOUND = 12

FIXED = 236
MIN_SIZE = 240
MAX_WIRE = 65535
MAX_OPTIONS = 64
COOKIE = 0x63825363
PAD = 0
END = 255


class DhcpError(Exception):
    def __init__(self, code: int) -> None:
        super().__init__(str(code))
        self.code = code


@dataclass
class Header:
    op: int = 0
    htype: int = 0
    hlen: int = 0
    hops: int = 0
    xid: int = 0
    secs: int = 0
    flags: int = 0
    ciaddr: int = 0
    yiaddr: int = 0
    siaddr: int = 0
    giaddr: int = 0
    chaddr: bytes = b"\x00" * 16
    sname: bytes = b"\x00" * 64
    file: bytes = b"\x00" * 128


@dataclass
class Option:
    off: int
    code: int
    length: int


@dataclass
class Message:
    wire: bytes
    header: Header = field(default_factory=Header)
    options: list[Option] = field(default_factory=list)
    pad_len: int = 0


def parse(wire: bytes) -> Message:
    if len(wire) > MAX_WIRE:
        raise DhcpError(TOO_LONG)
    if len(wire) < MIN_SIZE:
        raise DhcpError(SHORT)

    h = Header()
    h.op, h.htype, h.hlen, h.hops = wire[0], wire[1], wire[2], wire[3]
    (h.xid, h.secs, h.flags, h.ciaddr, h.yiaddr, h.siaddr, h.giaddr) = \
        struct.unpack_from(">IHHIIII", wire, 4)
    h.chaddr = bytes(wire[28:44])
    h.sname = bytes(wire[44:108])
    h.file = bytes(wire[108:236])

    if h.hlen > 16:
        raise DhcpError(BAD_HLEN)
    if struct.unpack_from(">I", wire, FIXED)[0] != COOKIE:
        raise DhcpError(BAD_COOKIE)

    msg = Message(wire=bytes(wire), header=h)
    pos = MIN_SIZE
    while True:
        if pos >= len(wire):
            raise DhcpError(NO_END)
        b = wire[pos]
        if b == END:
            pos += 1
            break
        if len(msg.options) >= MAX_OPTIONS:
            raise DhcpError(TOO_MANY)
        if b == PAD:
            msg.options.append(Option(pos, PAD, 0))
            pos += 1
            continue
        if pos + 1 >= len(wire):
            raise DhcpError(BAD_OPTION)
        length = wire[pos + 1]
        if length > len(wire) - (pos + 2):
            raise DhcpError(BAD_OPTION)
        msg.options.append(Option(pos, b, length))
        pos += 2 + length

    pad = 0
    while pos < len(wire):
        if wire[pos] != 0:
            raise DhcpError(TRAILING)
        pos += 1
        pad += 1
    msg.pad_len = pad
    return msg


def classify(wire: bytes) -> int:
    try:
        parse(bytes(wire))
        return OK
    except DhcpError as exc:
        return exc.code


def write(msg: Message) -> bytes:
    h = msg.header
    if h.hlen > 16:
        raise DhcpError(BADARG)
    if len(msg.options) > MAX_OPTIONS:
        raise DhcpError(BADARG)
    out = bytearray()
    out += bytes((h.op, h.htype, h.hlen, h.hops))
    out += struct.pack(">IHHIIII", h.xid, h.secs, h.flags,
                       h.ciaddr, h.yiaddr, h.siaddr, h.giaddr)
    out += h.chaddr + h.sname + h.file
    out += struct.pack(">I", COOKIE)
    for opt in msg.options:
        if opt.code == PAD:
            out.append(0)
            continue
        if opt.off >= len(msg.wire) or len(msg.wire) - opt.off < 2 + opt.length:
            raise DhcpError(BADARG)
        out += bytes((opt.code, opt.length))
        out += msg.wire[opt.off + 2 : opt.off + 2 + opt.length]
    out.append(END)
    out += b"\x00" * msg.pad_len
    return bytes(out)


def canon(wire: bytes) -> bytes:
    return write(parse(wire))


def fnv1a(data: bytes) -> int:
    h = 2166136261
    for b in data:
        h = ((h ^ b) * 16777619) & 0xFFFFFFFF
    return h


# ---- golden vectors -------------------------------------------------------
#
# Field values and option bytes are authored from the RFC; assemble() only
# packs them, and the parser never sees assemble(). The C suite uses the
# literal bytes printed by --hex.

def assemble(header: Header, options: bytes, pad: int = 0) -> bytes:
    out = bytearray()
    out += bytes((header.op, header.htype, header.hlen, header.hops))
    out += struct.pack(">IHHIIII", header.xid, header.secs, header.flags,
                       header.ciaddr, header.yiaddr, header.siaddr,
                       header.giaddr)
    out += header.chaddr + header.sname + header.file
    out += struct.pack(">I", COOKIE)
    out += options
    out.append(END)
    out += b"\x00" * pad
    return bytes(out)


def mac() -> bytes:
    return bytes.fromhex("000b8201fc42") + b"\x00" * 10


def hdr(op: int, xid: int, *, flags: int = 0, ciaddr: int = 0,
        yiaddr: int = 0, siaddr: int = 0, giaddr: int = 0) -> Header:
    return Header(op=op, htype=1, hlen=6, hops=0, xid=xid, secs=0,
                  flags=flags, ciaddr=ciaddr, yiaddr=yiaddr, siaddr=siaddr,
                  giaddr=giaddr, chaddr=mac())


GOLDEN: list[tuple[str, bytes]] = [
    ("discover", assemble(hdr(1, 0x3903F326, flags=0x8000), bytes.fromhex(
        "350101"                      # 53: DHCPDISCOVER
        "3703010306"                  # 55: subnet mask, router, DNS
        "3d0701000b8201fc42"          # 61: client identifier
    ))),
    ("offer", assemble(hdr(2, 0x3903F326, yiaddr=0xC0A80164,
                           siaddr=0xC0A80101), bytes.fromhex(
        "350102"                      # 53: DHCPOFFER
        "3604c0a80101"                # 54: server identifier
        "330400000e10"                # 51: lease time 3600
        "0104ffffff00"                # 1: subnet mask
        "0304c0a80101"                # 3: router
        "06080808080808080404"        # 6: DNS 8.8.8.8, 8.8.4.4
    ))),
    ("request", assemble(hdr(1, 0x12345678, flags=0x8000), bytes.fromhex(
        "350103"                      # 53: DHCPREQUEST
        "3204c0a80164"                # 50: requested IP
        "3604c0a80101"                # 54: server identifier
        "3d0701000b8201fc42"          # 61: client identifier
    ))),
    ("ack", assemble(hdr(2, 0x12345678, yiaddr=0xC0A80164,
                         siaddr=0xC0A80101), bytes.fromhex(
        "350105"                      # 53: DHCPACK
        "3604c0a80101"                # 54
        "330400000e10"                # 51
        "3a0400000708"                # 58: T1 1800
        "3b0400000a8c"                # 59: T2 2700
    ))),
    ("nak", assemble(hdr(2, 0x12345678), bytes.fromhex(
        "350106"                      # 53: DHCPNAK
        "3604c0a80101"                # 54
        "380c77726f6e67207375626e6574"  # 56: "wrong subnet"
    ))),
    ("release", assemble(hdr(1, 0x12345678, ciaddr=0xC0A80164),
                         bytes.fromhex(
        "350107"                      # 53: DHCPRELEASE
        "3604c0a80101"                # 54
    ))),
    ("inform", assemble(hdr(1, 0x12345678, ciaddr=0xC0A80164),
                        bytes.fromhex(
        "350108"                      # 53: DHCPINFORM
        "3703010306"                  # 55
    ))),
    # Interior PADs and five zero bytes after END must survive exactly.
    ("padded", assemble(hdr(1, 0x3903F326, flags=0x8000), bytes.fromhex(
        "00"                          # PAD before any option
        "350101"
        "00"                          # PAD between options
        "3703010306"
        "3d0701000b8201fc42"
        "00"                          # PAD before END
    ), pad=5)),
    ("minimal", assemble(hdr(1, 0x00000001, flags=0x8000), b"")),
]


def malformed_vectors() -> list[tuple[str, bytes, int]]:
    good = hdr(1, 0x00000001, flags=0x8000)
    base = assemble(good, b"")
    hlen17 = bytearray(base)
    hlen17[2] = 17
    bad_cookie = bytearray(base)
    bad_cookie[236] = 0x63
    bad_cookie[237:240] = b"\x53\x63\x64"
    opt_short = assemble(good, bytes.fromhex("350501"))       # len 5, 1 byte
    opt_nolen = assemble(good, bytes.fromhex("35"))
    no_end = bytearray(base)
    no_end = no_end[:240]                                      # header only
    no_end_opt = assemble(good, bytes.fromhex("350101"))[:-1]  # drop END
    trailing = assemble(good, b"") + b"\x01"
    too_many = assemble(good, b"\x00" * 65)                    # 65 PAD entries
    return [
        ("empty", b"", SHORT),
        ("short_239", b"\x00" * 239, SHORT),
        ("bad_cookie", bytes(bad_cookie), BAD_COOKIE),
        ("bad_hlen", bytes(hlen17), BAD_HLEN),
        ("option_past_end", opt_short, BAD_OPTION),
        ("option_no_length", opt_nolen, BAD_OPTION),
        ("no_end_header_only", bytes(no_end), NO_END),
        ("no_end_with_option", no_end_opt, NO_END),
        ("trailing_nonzero", trailing, TRAILING),
        ("too_many_options", too_many, TOO_MANY),
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

    for _, wire in GOLDEN:
        valid.append(wire)
        cases.append(wire)
    for _, wire, _ in malformed_vectors():
        cases.append(wire)

    for wire in valid:
        for n in range(len(wire) + 1):
            cases.append(wire[:n])

    rng = Lcg(0xD11C)
    for wire in valid:
        for _ in range(128):
            idx = rng.next() % len(wire)
            val = rng.next() & 0xFF
            mutated = bytearray(wire)
            mutated[idx] = val
            cases.append(bytes(mutated))
        for _ in range(32):
            idx = rng.next() % len(wire)
            val = rng.next() & 0xFF
            cases.append(wire[:idx] + bytes((val,)) + wire[idx:])
            cases.append(wire[:idx] + wire[idx + 1:])
    return cases


def expectation(wire: bytes) -> str:
    code = classify(wire)
    if code != OK:
        return f"{code} -1 00000000 -1"
    try:
        written = write(parse(wire))
    except DhcpError as exc:
        return f"{code} {exc.code} 00000000 -1"
    again = classify(written)
    return f"{code} {OK} {fnv1a(written):08x} {again}"


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
    for name, wire in GOLDEN:
        try:
            msg = parse(wire)
        except DhcpError as exc:
            print(f"FAIL {name}: parse rejected the golden vector ({exc.code})")
            failures += 1
            continue
        rewritten = write(msg)
        if rewritten != wire:
            print(f"FAIL {name}: write is not byte-identical")
            print(f"  in  {wire.hex()}")
            print(f"  out {rewritten.hex()}")
            failures += 1
            continue
        if classify(rewritten) != OK:
            print(f"FAIL {name}: rewrite does not parse")
            failures += 1
            continue
        print(f"PASS {name} ({len(wire)} bytes, {len(msg.options)} options)")

    for name, wire, want in malformed_vectors():
        got = classify(wire)
        if got != want:
            print(f"FAIL {name}: classified {got}, want {want}")
            failures += 1
        else:
            print(f"PASS {name} (status {got})")

    # Strong invariant: accepted messages reproduce exactly.
    for wire in make_corpus():
        if classify(wire) == OK:
            try:
                if write(parse(wire)) != wire:
                    print("FAIL corpus: accepted message not byte-identical")
                    failures += 1
                    break
            except DhcpError as exc:
                print(f"FAIL corpus: write failed after parse ({exc.code})")
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
    if len(argv) == 2 and argv[1] == "--hex":
        for name, wire in GOLDEN:
            print(f"{name} {len(wire)}")
            print(wire.hex())
        return 0
    print(__doc__)
    return 2


if __name__ == "__main__":
    sys.exit(main(sys.argv))
