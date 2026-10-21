#!/usr/bin/env python3
"""Independent oracle for the Winsock address corpus.

Uses Python's `ipaddress` (RFC-derived, not the C code) for parsing and
formatting, so a shared bug in the C parser cannot make both sides agree.
The command grammar matches ws_probe.c: `<command>\t<argument>`.
"""
import ipaddress
import sys


def parse(text):
    try:
        return ipaddress.IPv4Address(text)
    except (ipaddress.AddressValueError, ValueError):
        return None


def main():
    if len(sys.argv) != 2:
        raise SystemExit("usage: ws_ref.py <cases.txt>")
    with open(sys.argv[1], encoding="utf-8") as cases:
        for raw in cases:
            line = raw.rstrip("\n")
            command, sep, argument = line.partition("\t")
            if not sep:
                continue
            if command in ("pton", "addr"):
                address = parse(argument)
                if command == "addr":
                    value = "ffffffff" if address is None else f"{int(address):08x}"
                    print(f"addr\t{argument}\t{value}")
                elif address is None:
                    print(f"pton\t{argument}\t0")
                else:
                    print(f"pton\t{argument}\t1\t{int(address):08x}")
            elif command in ("ntop", "ntoa"):
                try:
                    text = str(ipaddress.IPv4Address(int(argument, 16)))
                except ValueError:
                    text = None
                if command == "ntoa":
                    print(f"ntoa\t{argument}\t{text}")
                elif text is None:
                    print(f"ntop\t{argument}\t0")
                else:
                    print(f"ntop\t{argument}\t1\t{text}")


if __name__ == "__main__":
    main()
