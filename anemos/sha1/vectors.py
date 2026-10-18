#!/usr/bin/env python3
"""Regenerate the expected digests in test_sha1.c.

The suite must not trust the implementation it tests, so every expected value
comes from Python's hashlib — an independent SHA-1. The first four entries
additionally match FIPS 180-1's published suite (§7.3-7.4) by inspection.

usage: python3 vectors.py        (prints C initialisers for the table)
"""
import hashlib

# (name, message, pattern-expr for test_sha1.c's make_msg)
CASES = [
    ("empty",      b"",                     "PAT_EMPTY", None),
    ("abc",        b"abc",                  "PAT_TEXT",  '"abc"'),
    ("two_block",  b"abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq",
     "PAT_TEXT", '"abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq"'),
    ("million_a",  b"a" * 1_000_000,        "PAT_A",      None),
    ("55_bytes",   b"x" * 55,               "PAT_X",      None),
    ("56_bytes",   b"x" * 56,               "PAT_X",      None),
    ("63_bytes",   b"x" * 63,               "PAT_X",      None),
    ("64_bytes",   b"x" * 64,               "PAT_X",      None),
    ("65_bytes",   b"x" * 65,               "PAT_X",      None),
    ("255_bytes",  bytes(range(255)),       "PAT_SEQ",    None),
    ("binary_ff",  b"\xff" * 100,           "PAT_FF",     None),
    ("nul_bytes",  b"\x00" * 64,            "PAT_ZERO",   None),
    ("alnum",      b"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789",
     "PAT_TEXT", '"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789"'),
]


def main() -> None:
    for name, data, pattern, text in CASES:
        digest = hashlib.sha1(data).hexdigest()
        text_expr = text if text is not None else "NULL"
        print(f'    {{ "{name}", {len(data):>7}, "{digest}", {pattern}, {text_expr} }},')


if __name__ == "__main__":
    main()
