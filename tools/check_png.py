#!/usr/bin/env python3
"""Decode a PNG with Python's own zlib and check it against the known image.

The encoder under test (src/native/control_png.c) is hand-written, so it must
be checked by something that shares none of its code. This reads the chunks,
verifies every CRC, inflates the IDAT stream and reproduces the exact gradient
tests/test_control_png.c wrote -- so a byte swap, a stride error or a bad CRC
is a mismatch here rather than a picture nobody can open.

    tools/check_png.py FILE.png
"""

import struct
import sys
import zlib

W, H = 61, 37


def expected(x, y):
    """Must match the gradient in tests/test_control_png.c, RGB order."""
    return ((x + y) & 0xFF, (y * 7) & 0xFF, (x * 4) & 0xFF)


def main():
    if len(sys.argv) != 2:
        raise SystemExit("usage: check_png.py FILE.png")
    data = open(sys.argv[1], "rb").read()
    if data[:8] != b"\x89PNG\r\n\x1a\n":
        raise SystemExit("check_png: not a PNG (bad signature)")

    pos, chunks, idat = 8, [], b""
    while pos < len(data):
        (n,) = struct.unpack(">I", data[pos:pos + 4])
        typ = data[pos + 4:pos + 8]
        body = data[pos + 8:pos + 8 + n]
        (crc,) = struct.unpack(">I", data[pos + 8 + n:pos + 12 + n])
        if zlib.crc32(typ + body) & 0xFFFFFFFF != crc:
            raise SystemExit("check_png: CRC mismatch in chunk %s" % typ)
        chunks.append(typ)
        if typ == b"IDAT":
            idat += body
        pos += 12 + n

    if chunks[0] != b"IHDR" or chunks[-1] != b"IEND":
        raise SystemExit("check_png: chunk order is %s" % chunks)

    w, h, depth, ctype = struct.unpack(">IIBB", data[16:26])
    if (w, h, depth, ctype) != (W, H, 8, 2):
        raise SystemExit("check_png: header says %dx%d depth %d type %d, "
                         "expected %dx%d depth 8 type 2"
                         % (w, h, depth, ctype, W, H))

    raw = zlib.decompress(idat)
    stride = w * 3 + 1
    if len(raw) != stride * h:
        raise SystemExit("check_png: inflated %d bytes, expected %d"
                         % (len(raw), stride * h))

    bad = 0
    for y in range(h):
        if raw[y * stride] != 0:
            raise SystemExit("check_png: row %d uses filter %d; this encoder "
                             "only emits filter 0" % (y, raw[y * stride]))
        for x in range(w):
            off = y * stride + 1 + x * 3
            got = tuple(raw[off:off + 3])
            if got != expected(x, y):
                bad += 1
                if bad <= 5:
                    print("  pixel (%d,%d): got %s expected %s"
                          % (x, y, got, expected(x, y)))
    if bad:
        raise SystemExit("check_png: %d of %d pixel(s) wrong"
                         % (bad, w * h))
    print("check_png: %dx%d decoded by zlib, all %d pixels match, every CRC ok"
          % (w, h, w * h))
    return 0


if __name__ == "__main__":
    sys.exit(main())
