#!/usr/bin/env python3
"""Extract igImage textures from an Alchemy IGB and decode them to PNG.

Uses the IGB format reader vendored under scratch/ref/igblib (KaikoClanworth1's
igb-blender, gitignored reference — NOT committed). This tool is our own code.

Usage:
    extract_font_igb.py <file.igb> <out_dir>

Writes one PNG per embedded mip level, named <stem>_<w>x<h>.png.
"""

import os
import struct
import sys
import zlib
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[1]
if str(REPO_ROOT / "scratch" / "ref") not in sys.path:
    sys.path.insert(0, str(REPO_ROOT / "scratch" / "ref"))


def write_png(rgba, width, height, path):
    def _chunk(t, d):
        c = t + d
        return struct.pack(">I", len(d)) + c + struct.pack(">I", zlib.crc32(c) & 0xFFFFFFFF)

    raw = bytearray()
    for y in range(height):
        raw.append(0)
        off = y * width * 4
        raw.extend(rgba[off:off + width * 4])
    out = b"\x89PNG\r\n\x1a\n"
    out += _chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 6, 0, 0, 0))
    out += _chunk(b"IDAT", zlib.compress(bytes(raw), 9))
    out += _chunk(b"IEND", b"")
    with open(path, "wb") as f:
        f.write(out)


def extract(igb_path):
    from igblib.igb_format.igb_reader import IGBReader
    from igblib.igb_format.igb_objects import IGBObject
    from igblib.scene_graph.sg_materials import extract_image
    from igblib.utils.image_convert import convert_image_to_rgba

    r = IGBReader(str(igb_path))
    r.read()
    out = {}
    for obj in r.objects:
        if isinstance(obj, IGBObject) and obj.is_type(b"igImage"):
            pi = extract_image(r, obj)
            if pi and pi.width and pi.pixel_data:
                rgba = convert_image_to_rgba(pi)
                if rgba is not None:
                    key = (pi.width, pi.height)
                    out[key] = (bytes(rgba), getattr(pi, "pixel_format", None))
    return out


def diff_images(a_rgba, b_rgba, w, h):
    """Return bbox of differing pixels between two RGBA buffers."""
    a = a_rgba[0]
    b = b_rgba[0]
    if len(a) != len(b):
        return None, len(a) != len(b)
    minx, miny, maxx, maxy = w, h, -1, -1
    count = 0
    for y in range(h):
        row_a = a[y * w * 4:(y + 1) * w * 4]
        row_b = b[y * w * 4:(y + 1) * w * 4]
        if row_a == row_b:
            continue
        for x in range(w):
            i = x * 4
            if row_a[i:i + 4] != row_b[i:i + 4]:
                count += 1
                if x < minx: minx = x
                if x > maxx: maxx = x
                if y < miny: miny = y
                if y > maxy: maxy = y
    if maxx < 0:
        return None, 0
    return (minx, miny, maxx + 1, maxy + 1), count


def main():
    if len(sys.argv) != 3:
        print(__doc__)
        sys.exit(1)
    src, outdir = sys.argv[1], sys.argv[2]
    os.makedirs(outdir, exist_ok=True)
    stem = Path(src).stem
    imgs = extract(src)
    for (w, h), (rgba, fmt) in sorted(imgs.items(), key=lambda kv: -kv[0][0]):
        p = os.path.join(outdir, f"{stem}_{w}x{h}.png")
        write_png(rgba, w, h, p)
        print(f"{w}x{h} fmt={fmt} -> {p}")


if __name__ == "__main__":
    main()
