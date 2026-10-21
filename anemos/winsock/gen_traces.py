#!/usr/bin/env python3
"""Deterministic socket-lifecycle traces: six hand-written scenarios plus
seeded random walks. No model is used here on purpose — the corpus is input,
not expected output; socket_ref.py supplies the expectation."""
import random
import sys
from pathlib import Path

SCENARIOS = {
    "lifecycle": [
        "socket\tstream", "state\t1",
        "bind\t1\t127.0.0.1\t8080", "state\t1",
        "listen\t1\t2", "listen\t1\t8", "state\t1",
        "incoming\t1", "incoming\t1",
        "accept\t1", "state\t2",
        "incoming\t1", "accept\t1", "accept\t1",
        "close\t1", "close\t2",
    ],
    "connect-ok": [
        "socket\tstream", "connect\t1\t10.0.0.1\t443", "state\t1",
        "connected\t1", "state\t1",
        "shutdown\t1\t2", "shutdown\t1\t0",
        "close\t1", "state\t1",
    ],
    "connect-refused": [
        "socket\tstream", "connect\t1\t10.0.0.1\t80", "refused\t1",
        "state\t1", "connect\t1\t10.0.0.1\t81",
        "connected\t1", "state\t1", "close\t1",
    ],
    "errors": [
        "bind\t9\t0.0.0.0\t53", "socket\tdgram", "state\t1",
        "listen\t1\t1", "connect\t1\t1.2.3.4\t53",
        "bind\t1\t1.1.1.1\t53", "socket\tdgram",
        "bind\t2\t2.2.2.2\t53", "bind\t2\t2.2.2.2\t54",
        "listen\t2\t1", "shutdown\t1\t1", "accept\t2",
        "close\t1", "close\t1", "socket\tbogus",
        "bind\t3\t300.1.1.1\t53", "socket\tstream", "state\t3",
    ],
    "backlog": [
        "socket\tstream", "bind\t1\t0.0.0.0\t0", "listen\t1\t1",
        "incoming\t1", "incoming\t1",
        "accept\t1", "state\t2", "incoming\t1",
        "accept\t1", "state\t3", "close\t2", "accept\t1", "state\t3",
    ],
    "implicit-bind": [
        "socket\tstream", "connect\t1\t1.2.3.4\t80", "state\t1",
        "socket\tstream", "connect\t2\t1.2.3.4\t81", "state\t2",
        "connected\t1", "connected\t2",
        "shutdown\t1\t3", "close\t1", "close\t2",
    ],
}

SCENARIOS["readiness"] = [
    "socket\tstream", "connect\t1\t10.0.0.1\t80", "connected\t1",
    "select\t1\t1", "writable\t1", "select\t1\t1",
    "readable\t1\t30", "select\t1\t1",
    "send\t1\t0", "recv\t1\t0",
    "recv\t1\t10", "select\t1\t1",
    "recv\t1\t25", "send\t1\t100", "send\t1\t100",
    "writable\t1", "send\t1\t100",
    "shutdown\t1\t1", "send\t1\t10",
    "eof\t1", "select\t1\t1", "recv\t1\t10",
    "close\t1",
]
SCENARIOS["reset"] = [
    "socket\tstream", "connect\t1\t10.0.0.1\t80", "connected\t1",
    "send\t1\t5", "reset\t1", "send\t1\t5", "recv\t1\t5",
    "select\t1\t1", "close\t1",
]
SCENARIOS["select-errors"] = [
    "select\t9\t", "select\t\t9", "select\t1,2\t",
    "socket\tstream", "select\t1\t", "select\t\t1",
    "connect\t1\t10.0.0.1\t80", "select\t1\t1",
    "connected\t1", "select\t1\t1", "select\t1,1\t1,1",
    "close\t1",
]
SCENARIOS["parse-edges"] = [
    "socket\tstream", "close\t1x", "state\t1",
    "listen\t1\t 2", "bind\t1\t127.0.0.1\t+80",
    "bind\t1\t127.0.0.1\t65536", "shutdown\t1\t-1",
    "close\t1", "state\t1",
]
SCENARIOS["broadcast"] = [
    "socket\tstream", "bind\t1\t255.255.255.255\t80",
    "bind\t1\t0.0.0.0\t80", "state\t1",
]
SCENARIOS["ephemeral-skip"] = [
    "socket\tstream", "bind\t1\t0.0.0.0\t49152",
    "socket\tstream", "connect\t2\t10.0.0.1\t80", "state\t2",
]
SCENARIOS["dgram-connect"] = [
    "socket\tdgram", "bind\t1\t0.0.0.0\t0", "connect\t1\t10.0.0.1\t53",
    "state\t1", "connected\t1", "state\t1",
    "send\t1\t100", "writable\t1", "send\t1\t100",
    "select\t1\t1", "shutdown\t1\t1", "send\t1\t100", "close\t1",
]
SCENARIOS["exhaustion"] = (
    ["socket\tstream"] * 66
    + ["state\t65", "close\t1", "socket\tstream", "state\t65"]
)


def random_trace(rng, length=60):
    lines = []
    for _ in range(length):
        command = rng.choices(
            ["socket", "bind", "listen", "connect", "accept", "shutdown",
             "close", "state", "connected", "refused", "incoming",
             "send", "recv", "readable", "writable", "eof", "reset",
             "select"],
            weights=[4, 6, 4, 5, 4, 3, 4, 6, 3, 3, 4, 5, 5, 5, 3, 3, 3, 5])[0]
        ident = rng.choice([0, 1, 1, 2, 2, 3, 3, 4, 5, 6, 7, 99])
        if command == "socket":
            lines.append(f"socket\t{rng.choice(['stream', 'dgram', 'bogus'])}")
        elif command in ("bind", "connect"):
            address = rng.choice(["127.0.0.1", "0.0.0.0", "10.0.0.1",
                                  "1.2.3.4", "300.0.0.1", "1.2.3"])
            port = rng.choice([0, 53, 80, 443, 8080])
            lines.append(f"{command}\t{ident}\t{address}\t{port}")
        elif command == "listen":
            lines.append(f"listen\t{ident}\t{rng.choice([0, 1, 2, 8])}")
        elif command == "shutdown":
            lines.append(f"shutdown\t{ident}\t{rng.choice([0, 1, 2, 3])}")
        elif command in ("send", "recv"):
            lines.append(f"{command}\t{ident}\t{rng.choice([0, 1, 5, 100, 4096])}")
        elif command == "readable":
            lines.append(f"readable\t{ident}\t{rng.choice([0, 1, 30, 4096])}")
        elif command in ("writable", "eof", "reset"):
            lines.append(f"{command}\t{ident}")
        elif command == "select":
            def ids():
                picks = rng.sample([0, 1, 2, 3, 4, 5, 6, 7, 99],
                                   rng.choice([0, 1, 1, 2]))
                return ",".join(str(x) for x in picks)
            lines.append(f"select\t{ids()}\t{ids()}")
        elif command in ("accept", "close", "state", "connected", "refused",
                         "incoming"):
            lines.append(f"{command}\t{ident}")
    return lines


def main():
    out = Path(sys.argv[1]) if len(sys.argv) > 1 else Path("traces")
    out.mkdir(parents=True, exist_ok=True)
    for name, lines in SCENARIOS.items():
        (out / f"{name}.txt").write_text("\n".join(lines) + "\n", encoding="utf-8")
    rng = random.Random(20261021)
    for index in range(20):
        lines = random_trace(rng)
        (out / f"random-{index:02d}.txt").write_text(
            "\n".join(lines) + "\n", encoding="utf-8")


if __name__ == "__main__":
    main()
