#!/usr/bin/env python3
"""Writes a plain 512x512 PNG (the real icon comes with the visual identity). Usage: make-placeholder-icon.py OUT"""
import struct
import sys
import zlib

WIDTH = HEIGHT = 512
COLOR = bytes((0x10, 0x14, 0x18))


def chunk(kind, data):
    body = kind + data
    return struct.pack(">I", len(data)) + body + struct.pack(">I", zlib.crc32(body) & 0xFFFFFFFF)


rows = b"".join(b"\x00" + COLOR * WIDTH for _ in range(HEIGHT))
png = (b"\x89PNG\r\n\x1a\n"
       + chunk(b"IHDR", struct.pack(">IIBBBBB", WIDTH, HEIGHT, 8, 2, 0, 0, 0))
       + chunk(b"IDAT", zlib.compress(rows, 9))
       + chunk(b"IEND", b""))
with open(sys.argv[1], "wb") as out:
    out.write(png)
