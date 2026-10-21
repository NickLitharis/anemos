#!/usr/bin/env python3
"""Deterministic Winsock address corpus: fixed valid/invalid forms plus seeded
random addresses. Tab separates the command from the argument so whitespace
cases survive intact."""
import random

VALID = [
    "127.0.0.1", "0.0.0.0", "255.255.255.255", "192.168.1.1", "10.0.0.1",
    "1.2.3.4", "8.8.8.8", "169.254.1.2", "224.0.0.251", "172.16.254.1",
    "0.0.0.1", "1.0.0.0", "12.34.56.78", "100.200.250.255",
]

INVALID = [
    "", "1.2.3", "1.2.3.4.5", "256.0.0.1", "1.2.3.256", "1.2.3.-1",
    "+1.2.3.4", "1.2.3.4 ", " 1.2.3.4", "1..2.3", "1.2.3.", ".1.2.3",
    "1.2.3.04", "01.2.3.4", "000.0.0.0", "1.2.3.4x", "999.1.1.1",
    "1.2.3.1000", "0x7f.0.0.1", "1,2,3,4", "1.2.3.4.0", "1.2.3.4\tx",
    "1.2.3.4\n5", "1.2.3.+4", "1.2.3.4 ", "1.2.3.4.",
]


def main():
    rng = random.Random(20261021)
    lines = []
    for text in VALID + INVALID:
        lines.append(f"pton\t{text}")
        lines.append(f"addr\t{text}")
    # A newline inside an argument would split the line; keep the corpus
    # grammar one case per line.
    lines = [line for line in lines if "\n" not in line]

    values = [0, 0xFFFFFFFF, 0x7F000001, 0xC0A80101]
    values += [rng.getrandbits(32) for _ in range(200)]
    for value in values:
        text = f"{value:08x}"
        lines.append(f"ntop\t{text}")
        lines.append(f"ntoa\t{text}")

    for _ in range(100):
        octets = [rng.randrange(256) for _ in range(4)]
        lines.append("pton\t" + ".".join(str(o) for o in octets))

    print("\n".join(lines))


if __name__ == "__main__":
    main()
