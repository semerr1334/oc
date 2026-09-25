#!/usr/bin/env python3
"""packfs.py OUT IN [IN ...] — сборка образа файловой системы OCFS.

Формат: 'OCFS', u32 count, потом count * (name[32], u32 off, u32 len),
далее сами файлы (off — от начала образа).
"""
import os
import struct
import sys


def main():
    out, *inputs = sys.argv[1:]
    table_size = 8 + 40 * len(inputs)
    table = b""
    body = b""
    off = table_size
    for path in inputs:
        name = os.path.basename(path).encode("utf-8")[:31]
        blob = open(path, "rb").read()
        table += name.ljust(32, b"\0") + struct.pack("<II", off, len(blob))
        body += blob
        off += len(blob)
    with open(out, "wb") as f:
        f.write(b"OCFS" + struct.pack("<I", len(inputs)) + table + body)
    print(f"ocfs: {len(inputs)} файлов, {off} байт -> {out}")


if __name__ == "__main__":
    main()
