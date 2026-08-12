#!/usr/bin/env python3
"""Extract igImage textures from an Alchemy IGB and decode them to PNG.

Uses the IGB format reader vendored under scratch/ref/igblib (KaikoClanworth1's
igb-blender, gitignored reference — NOT committed). This tool is our own code.

Usage:
    extract_font_igb.py <file.igb> <out_dir>

Writes one PNG per embedded igImage, named <stem>_<index>_<w>x<h>.png, and
prints how many there were. The index is in the name because a font IGB holds
several images at the same size and a name without it loses all but one.
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
    # A LIST, not a dict keyed on (width, height).
    #
    # It was a dict, and an IGB holding several igImages of the same size had
    # them overwrite each other -- only the last survived. Two fonts that
    # differ in 82% of their bytes then decoded to "byte-identical" atlases and
    # a feature was written off on the strength of it (instrument I045). Every
    # image is emitted now, with its index, and the count is printed so that
    # "one image" is a fact rather than an assumption.
    out = []
    skipped = 0
    for obj in r.objects:
        if isinstance(obj, IGBObject) and obj.is_type(b"igImage"):
            pi = extract_image(r, obj)
            if not (pi and pi.width and pi.pixel_data):
                skipped += 1
                continue
            rgba = convert_image_to_rgba(pi)
            if rgba is None:
                skipped += 1
                continue
            out.append((pi.width, pi.height, bytes(rgba),
                        getattr(pi, "pixel_format", None)))
    if skipped:
        # Named rather than dropped: an image this cannot decode is a hole in
        # any comparison made from the output.
        print(f"WARNING: {skipped} igImage(s) could not be decoded and are NOT "
              f"in the output; any 'these two are the same' conclusion drawn "
              f"from it is unsound.")
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
    print(f"{len(imgs)} igImage(s) decoded from {src}")
    for i, (w, h, rgba, fmt) in enumerate(imgs):
        p = os.path.join(outdir, f"{stem}_{i:02d}_{w}x{h}.png")
        write_png(rgba, w, h, p)
        print(f"[{i:02d}] {w}x{h} fmt={fmt} -> {p}")


if __name__ == "__main__":
    main()
