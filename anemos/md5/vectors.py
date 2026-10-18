#!/usr/bin/env python3
"""Regenerate the expected digests in test_md5.c.

The suite must not trust the implementation it tests, so every expected value
comes from Python's hashlib — an independent MD5. The first seven entries
additionally match RFC 1321's published suite (§A.5) by inspection.

usage: python3 vectors.py        (prints C initialisers for the table)
"""
import hashlib

# (name, message, pattern-expr for test_md5.c's make_msg)
CASES = [
    ("empty",          b"",                     "PAT_EMPTY", None),
    ("a",              b"a",                    "PAT_TEXT",  '"a"'),
    ("abc",            b"abc",                  "PAT_TEXT",  '"abc"'),
    ("message digest", b"message digest",       "PAT_TEXT",  '"message digest"'),
    ("alpha",          b"abcdefghijklmnopqrstuvwxyz", "PAT_TEXT", '"abcdefghijklmnopqrstuvwxyz"'),
    ("alnum",          b"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789",
     "PAT_TEXT", '"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789"'),
    ("digits10x8",     b"1234567890" * 8,       "PAT_DIGITS", None),
    ("million_a",      b"a" * 1_000_000,        "PAT_A",      None),
    ("55_bytes",       b"x" * 55,               "PAT_X",      None),
    ("56_bytes",       b"x" * 56,               "PAT_X",      None),
    ("63_bytes",       b"x" * 63,               "PAT_X",      None),
    ("64_bytes",       b"x" * 64,               "PAT_X",      None),
    ("65_bytes",       b"x" * 65,               "PAT_X",      None),
    ("255_bytes",      bytes(range(255)),       "PAT_SEQ",    None),
    ("binary_ff",      b"\xff" * 100,           "PAT_FF",     None),
    ("nul_bytes",      b"\x00" * 64,            "PAT_ZERO",   None),
]


def main() -> None:
    for name, data, pattern, text in CASES:
        digest = hashlib.md5(data).hexdigest()
        text_expr = text if text is not None else "NULL"
        print(f'    {{ "{name}", {len(data):>7}, "{digest}", {pattern}, {text_expr} }},')


if __name__ == "__main__":
    main()
