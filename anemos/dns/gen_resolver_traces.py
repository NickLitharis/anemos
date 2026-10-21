#!/usr/bin/env python3
"""Deterministic resolver traces. Response bytes are built here with the P2
Python client model, then embedded as hex, so both harnesses inject identical
frames and only the glue is under test.

Grammar: `init <seed> <attempts> <timeout>`, then
`start <name> <type> <server> <now>`, `packet <hex> <now>`, `timer <now>`,
`status`, `client`, `result`.
"""
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))

import dns_client_ref as dc  # noqa: E402

SERVER = "8.8.8.8"
FLAGS = dc.FLAG_QR | dc.FLAG_RA
A_ADDR = bytes((192, 0, 2, 1))
AAAA_ADDR = bytes.fromhex("20010db8000000000000000000000001")
TTL = 300


def lcg(seed):
    return (seed * 1664525 + 1013904223) & 0xFFFFFFFF


def ident_first(seed):
    return dc.id_for_seed(seed)


def ident_second(seed):
    return dc.id_for_seed(lcg(seed))


def init(seed, attempts, timeout):
    return f"init\t{seed}\t{attempts}\t{timeout}"


def start(name, qtype, now=0, server=SERVER):
    return f"start\t{name}\t{qtype}\t{server}\t{now}"


def packet(wire, now):
    return f"packet\t{wire.hex()}\t{now}"


def answer(name, qtype, ident, answers, flags=FLAGS):
    return dc.mk_response(name, qtype, ident, flags, answers)


def a_answer(name, ident, ip=A_ADDR):
    return answer(name, dc.TYPE_A, ident, [(name, dc.TYPE_A, TTL, ip)])


def cases():
    out = {}

    name = "www.example.com"
    out["a-basic"] = [
        init(1, 2, 1000), start(name, "A"),
        packet(a_answer(name, ident_first(1)), 10),
        "status", "result",
    ]

    name = "v6.example.com"
    out["aaaa-basic"] = [
        init(7, 2, 1000), start(name, "AAAA"),
        packet(answer(name, dc.TYPE_AAAA, ident_first(7),
                      [(name, dc.TYPE_AAAA, TTL, AAAA_ADDR)]), 10),
        "result",
    ]

    name = "wrong.example.com"
    out["wrong-id"] = [
        init(3, 2, 1000), start(name, "A"),
        packet(a_answer(name, (ident_first(3) + 1) & 0xFFFF), 5),
        "status",
        packet(a_answer(name, ident_first(3)), 10),
        "result",
    ]

    name = "nx.example.com"
    out["rcode"] = [
        init(4, 2, 1000), start(name, "A"),
        packet(answer(name, dc.TYPE_A, ident_first(4), [], flags=FLAGS | 3), 5),
        "status", "client", "result",
    ]

    name = "tc.example.com"
    out["truncated"] = [
        init(5, 2, 1000), start(name, "A"),
        packet(answer(name, dc.TYPE_A, ident_first(5), [],
                      flags=FLAGS | dc.FLAG_TC), 5),
        "status", "client", "result",
    ]

    name = "bad.example.com"
    good = a_answer(name, ident_first(6))
    out["malformed"] = [
        init(6, 2, 1000), start(name, "A"),
        packet(good[:15], 5),
        "status", "client",
        "timer\t1000",
        packet(good, 1010),
        "result",
    ]

    out["timeout"] = [
        init(8, 2, 500), start("slow.example.com", "A"),
        "timer\t499", "status",
        "timer\t500", "status",
        "timer\t999", "status",
        "timer\t1000", "status", "result",
    ]

    name = "alias.example.com"
    target = "target.example.com"
    out["cname-chain"] = [
        init(9, 2, 1000), start(name, "A"),
        packet(answer(name, dc.TYPE_A, ident_first(9), [
            (name, dc.TYPE_CNAME, TTL, dc.enc_name(target)),
            (target, dc.TYPE_A, TTL, A_ADDR),
        ]), 5),
        "result",
    ]

    name = "only.example.com"
    target = "nowhere.example.com"
    out["cname-only"] = [
        init(10, 2, 1000), start(name, "A"),
        packet(answer(name, dc.TYPE_A, ident_first(10), [
            (name, dc.TYPE_CNAME, TTL, dc.enc_name(target)),
        ]), 5),
        "status", "client", "result",
    ]

    name = "empty.example.com"
    out["no-answer"] = [
        init(11, 2, 1000), start(name, "A"),
        packet(answer(name, dc.TYPE_A, ident_first(11), []), 5),
        "status", "client", "result",
    ]

    out["bad-name"] = [
        init(12, 2, 1000), start("bad..name", "A"),
        "status", "result",
    ]

    out["bad-server"] = [
        init(22, 2, 1000),
        start("server.example.com", "A", server="300.1.1.1"),
        "status", "result",
    ]

    out["abandon"] = [
        init(23, 2, 1000), start("abandon.example.com", "A"),
        "status",
        start("bad..name", "A", now=5),
        "status", "result",
    ]

    name = "restart.example.com"
    out["restart"] = [
        init(21, 2, 1000), start(name, "A", 0),
        packet(a_answer(name, ident_first(21)), 5), "status",
        start(name, "A", 100),
        packet(a_answer(name, ident_second(21)), 105), "result",
    ]

    out["pre-init"] = [
        "packet\t00\t0", "status", "result",
    ]

    out["unsupported-type"] = [
        init(24, 2, 1000), start("mx.example.com", "MX"),
        "status", "result",
    ]

    name = "www.example.com."
    out["trailing-dot"] = [
        init(25, 2, 1000), start(name, "A"),
        packet(a_answer(name, ident_first(25)), 5),
        "result",
    ]

    name = "huge.example.com"
    out["huge-values"] = [
        init(4294967297, 4294967298, 4294967296), start(name, "A", 0),
        packet(a_answer(name, ident_first(1)), 10),
        "result",
    ]

    return out


def main():
    out_dir = Path(sys.argv[1]) if len(sys.argv) > 1 else Path("traces")
    out_dir.mkdir(parents=True, exist_ok=True)
    for name, lines in cases().items():
        (out_dir / f"{name}.txt").write_text("\n".join(lines) + "\n",
                                            encoding="utf-8")


if __name__ == "__main__":
    main()
