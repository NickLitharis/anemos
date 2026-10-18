#!/usr/bin/env python3
"""Regenerate the tables in anemos/nls/nls.c.

The suite must not trust the implementation it tests: the CP1252 table here
comes from Python's cp1252 codec — an independent implementation of the
Unicode Consortium's published mapping — and the test file states its
expectations as Unicode constants separately.

usage: python3 vectors.py        (prints C initialisers)
"""


def forward_table() -> list[int]:
    out = []
    for b in range(256):
        try:
            out.append(ord(bytes([b]).decode("cp1252")))
        except UnicodeDecodeError:
            out.append(0xFFFF)
    return out


def main() -> None:
    t = forward_table()
    undef = [b for b in range(256) if t[b] == 0xFFFF]
    print(f"/* undefined: {[hex(b) for b in undef]} */")
    print("static const uint16_t k_cp1252_to_uni[256] = {")
    for row in range(0, 256, 8):
        cells = ", ".join(f"0x{t[j]:04x}" for j in range(row, row + 8))
        print(f"    {cells},")
    print("};")
    print()
    specials = sorted((t[b], b) for b in range(0x80, 0xA0) if t[b] != 0xFFFF)
    print(f"/* {len(specials)} defined entries in 0x80..0x9f, sorted by codepoint */")
    print("static const struct { uint16_t uni; uint8_t byte; } k_specials[] = {")
    for u, b in specials:
        print(f"    {{ 0x{u:04x}, 0x{b:02x} }},")
    print("};")


if __name__ == "__main__":
    main()
