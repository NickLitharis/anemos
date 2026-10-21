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


def legacy_part(text):
    if text == "":
        return None
    if text.lower().startswith("0x"):
        base, digits = 16, text[2:]
    elif len(text) > 1 and text[0] == "0":
        base, digits = 8, text[1:]
    else:
        base, digits = 10, text
    alphabet = "0123456789abcdef"[:base if base <= 10 else 16]
    if digits == "" or any(ch.lower() not in alphabet for ch in digits):
        return None
    value = int(digits, base)
    return value if 0 <= value <= 0xFFFFFFFF else None


def legacy_addr(text):
    """BSD/XP inet_addr, including the leading-space quirk (returns 0)."""
    if text.startswith(" "):
        return 0
    parts = text.split(".")
    if not 1 <= len(parts) <= 4:
        return None
    values = []
    for part in parts:
        value = legacy_part(part)
        if value is None:
            return None
        values.append(value)
    for value in values[:-1]:
        if value > 255:
            return None
    if len(values) == 2:
        if values[1] > 0xFFFFFF: return None
        return (values[0] << 24) | values[1]
    if len(values) == 3:
        if values[2] > 0xFFFF: return None
        return (values[0] << 24) | (values[1] << 16) | values[2]
    if len(values) == 4:
        if values[3] > 255: return None
        return (values[0] << 24) | (values[1] << 16) | (values[2] << 8) | values[3]
    return values[0]


def main():
    if len(sys.argv) != 2:
        raise SystemExit("usage: ws_ref.py <cases.txt>")
    with open(sys.argv[1], encoding="utf-8") as cases:
        for raw in cases:
            line = raw.rstrip("\n")
            command, sep, argument = line.partition("\t")
            if not sep:
                continue
            if command == "addr":
                legacy = legacy_addr(argument)
                value = "ffffffff" if legacy is None else f"{legacy:08x}"
                print(f"addr\t{argument}\t{value}")
            elif command == "pton":
                address = parse(argument)
                if address is None:
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
