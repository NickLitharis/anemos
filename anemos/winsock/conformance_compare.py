#!/usr/bin/env python3
"""Compare two Winsock observation certificates, case by case.

Both files are `name value` lines in the order the probe emits them. The
guest file is XP (the oracle); the host file is our implementation. Any
missing, extra or differing case is a conformance finding, and the process
exits non-zero.
"""
import sys


def load(path):
    cases = {}
    order = []
    with open(path, encoding="utf-8", errors="replace") as handle:
        for raw in handle:
            line = raw.strip()
            if not line or line.startswith("all "):
                continue
            name, _, value = line.partition(" ")
            cases[name] = value
            order.append(name)
    return cases, order


def main():
    if len(sys.argv) != 3:
        raise SystemExit("usage: conformance_compare.py <xp-cert> <host-cert>")
    xp, xp_order = load(sys.argv[1])
    host, _ = load(sys.argv[2])
    mismatches = 0
    for name in xp_order:
        if name not in host:
            print(f"MISSING {name} xp={xp[name]} host=-")
            mismatches += 1
    for name in host:
        if name not in xp:
            print(f"EXTRA {name} host={host[name]}")
            mismatches += 1
    for name in xp_order:
        if name in host and host[name] != xp[name]:
            print(f"DIFF {name} xp={xp[name]} host={host[name]}")
            mismatches += 1
    if mismatches == 0:
        print(f"IDENTICAL {len(xp_order)} cases")
        return 0
    print(f"{mismatches} difference(s) across {len(xp_order)} cases")
    return 1


if __name__ == "__main__":
    raise SystemExit(main())
