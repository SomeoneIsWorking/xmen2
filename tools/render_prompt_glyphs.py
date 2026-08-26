#!/usr/bin/env python3
"""Rasterise the shared prompt SVGs into a PORT-OWNED atlas header.

    python3 tools/render_prompt_glyphs.py <out-header>
    python3 tools/render_prompt_glyphs.py --selftest

This is the build-time half of the renderer-side prompt feature: the game's
fonts stay untouched, and the port draws its own controller/keycap art at the
text-renderer override (src/native/prompt_glyph_draw.c). The art comes from
the shared `port-assets` sets named by assets/buttons/glyphs.json -- the same
manifest and the same metric semantics tools/make_pad_font.py publishes (18px
design cell, advances 19 / 4 / 8 / -8 / 4), so the label composition in
prompt_labels.c keeps its exact meaning. What changes is WHO owns the pixels:
a generated header in this repo, never a patched copy of a shipped font.

The atlas is rasterised at 4x supersample so a glyph stays crisp when the
runtime draws it larger than 18 design pixels (the AUTO text scale is 2.64 at
2160p on this install).
"""

from __future__ import annotations

import argparse
from io import BytesIO
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))

from pad_glyph_manifest import (FIRST_CODEPOINT, ICONS,          # noqa: E402
                                KEYCAP_FIRST_CODEPOINT, KEYCAP_PARTS,
                                keycap_svg_path, svg_paths)

from PIL import Image                                            # noqa: E402
from resvg_py import svg_to_bytes                                # noqa: E402

# Design units are FONT PIXELS: the shipped pack's cell and advances, which
# prompt_labels.c's composition was verified against (#91).
CELL = 18
PAD_ADVANCE = CELL + 1
KEYCAP_METRICS = {"left": (5, 4), "middle": (12, 8),
                  "rewind": (0, -8), "right": (5, 4)}
SS = 4                       # raster supersample per design pixel

ATLAS_W = 512


def rasterise_icon(src: Path) -> tuple[bytes, int, int]:
    """One icon -> RGBA bytes at CELL*SS square, plus its design box."""
    size = CELL * SS
    try:
        png = svg_to_bytes(svg_path=str(src), width=size, height=size)
        with Image.open(BytesIO(png)) as image:
            image.load()
            if image.size != (size, size):
                raise ValueError("renderer returned %dx%d" % image.size)
            px = image.convert("RGBA").tobytes()
    except Exception as error:
        raise SystemExit(f"REFUSING: rasterising {src} failed: {error}") from error
    if len(px) != size * size * 4:
        raise SystemExit(f"REFUSING: {src} rasterised to {len(px)} bytes, "
                         f"not the {size * size * 4} a {size}x{size} RGBA is.")
    if not any(px[i * 4 + 3] > 8 for i in range(size * size)):
        raise SystemExit(f"REFUSING: {src} rasterised fully transparent -- it "
                         "would publish as a blank prompt.")
    return px, CELL, CELL


def rasterise_keycap_pieces(src: Path) -> dict[str, tuple[bytes, int]]:
    """Shared blank cap -> left/middle/right slices at SS, luminance-flipped.

    Same slicing as make_pad_font.rasterise_keycap (middle from the unrounded
    centre, overlapping at an eight-design-pixel advance), and the same
    luminance inversion, because the stock letters that draw over the cap are
    bright while the shared SVG's face is light."""
    width, height = 45 * SS, CELL * SS
    try:
        png = svg_to_bytes(svg_path=str(src), width=width, height=height)
        with Image.open(BytesIO(png)) as image:
            image.load()
            if image.size != (width, height):
                raise ValueError("renderer returned %dx%d" % image.size)
            pixels = bytearray(image.convert("RGBA").tobytes())
    except Exception as error:
        raise SystemExit(f"REFUSING: rasterising keycap {src} failed: {error}") from error
    for i in range(width * height):
        at = i * 4
        if pixels[at + 3] == 0:
            continue
        lum = (pixels[at] * 54 + pixels[at + 1] * 183 + pixels[at + 2] * 19) // 256
        value = max(28, min(225, 238 - lum))
        pixels[at:at + 3] = bytes((value, value, value))

    def column_slice(x0_ss: int, piece_design_w: int) -> bytes:
        w_ss = piece_design_w * SS
        out = bytearray()
        for y in range(height):
            start = (y * width + x0_ss) * 4
            out.extend(pixels[start:start + w_ss * 4])
        return bytes(out)

    pieces = {}
    for name, (design_w, _advance) in KEYCAP_METRICS.items():
        if not design_w:
            continue
        if name == "left":
            x0 = 0
        elif name == "middle":
            x0 = 17 * SS
        else:
            x0 = width - design_w * SS
        pieces[name] = (column_slice(x0, design_w), design_w)
    return pieces


def build() -> tuple[int, int, bytes, list[dict]]:
    """Atlas pixels + one entry per published codepoint, in codepoint order."""
    icons = [rasterise_icon(p) for p in svg_paths()]
    caps = rasterise_keycap_pieces(keycap_svg_path())

    cell = CELL * SS
    gap = SS                                   # one design pixel between cells
    cells: list[tuple[dict, bytes, int, int]] = []   # entry, pixels, w_ss, h_ss
    entries: list[dict] = []
    x, y, row_h = 0, 0, 0

    def place(w_ss: int, h_ss: int) -> tuple[int, int]:
        nonlocal x, y, row_h
        if x and x + w_ss > ATLAS_W:
            x, y, row_h = 0, y + row_h + gap, 0
        px, py = x, y
        x += w_ss + gap
        row_h = max(row_h, h_ss)
        return px, py

    for i, (px, _, _) in enumerate(icons):
        ax, ay = place(cell, cell)
        entry = {"name": ICONS[i], "code": FIRST_CODEPOINT + i,
                 "x": ax, "y": ay, "w": cell, "h": cell,
                 "design_w": CELL, "design_h": CELL,
                 "advance": PAD_ADVANCE}
        entries.append(entry)
        cells.append((entry, px, cell, cell))
    for index, name in enumerate(KEYCAP_PARTS):
        code = KEYCAP_FIRST_CODEPOINT + index
        design_w, advance = KEYCAP_METRICS[name]
        if not design_w:
            # Rewind is invisible by design; it carries only its advance.
            entries.append({"name": "keycap_" + name, "code": code,
                            "x": 0, "y": 0, "w": 0, "h": 0,
                            "design_w": 0, "design_h": 0, "advance": advance})
            continue
        px, pw = caps[name]
        ax, ay = place(pw * SS, cell)
        entry = {"name": "keycap_" + name, "code": code,
                 "x": ax, "y": ay, "w": pw * SS, "h": cell,
                 "design_w": design_w, "design_h": CELL,
                 "advance": advance}
        entries.append(entry)
        cells.append((entry, px, pw * SS, cell))

    atlas_h = y + row_h
    atlas = bytearray(ATLAS_W * atlas_h * 4)
    for entry, px, aw, ah in cells:
        for row in range(ah):
            src_at = (row * aw) * 4
            dst_at = ((entry["y"] + row) * ATLAS_W + entry["x"]) * 4
            atlas[dst_at:dst_at + aw * 4] = px[src_at:src_at + aw * 4]
    return ATLAS_W, atlas_h, bytes(atlas), entries


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--selftest", action="store_true")
    parser.add_argument("out", nargs="?", type=Path)
    args = parser.parse_args()

    if args.selftest:
        # The positive case that must come out positive: the manifest
        # resolves, every SVG rasterises to ink, and the metrics keep the
        # composition semantics prompt_labels.c was verified against.
        paths = svg_paths()
        if len(paths) != 16 or len(rasterise_icon(paths[0])[0]) == 0:
            raise SystemExit("pad glyph atlas selftest: icon set broken")
        caps = rasterise_keycap_pieces(keycap_svg_path())
        if set(caps) != {"left", "middle", "right"}:
            raise SystemExit("pad glyph atlas selftest: keycap slicing broken")
        w, h, atlas, entries = build()
        if not any(atlas[i * 4 + 3] for i in range(len(atlas) // 4)):
            raise SystemExit("pad glyph atlas selftest: atlas has no ink")
        by_name = {e["name"]: e for e in entries}
        if by_name["keycap_rewind"]["advance"] != -8:
            raise SystemExit("pad glyph atlas selftest: rewind lost its "
                             "negative advance")
        if by_name["face_a"]["advance"] != PAD_ADVANCE:
            raise SystemExit("pad glyph atlas selftest: pad advance changed")
        print("pad glyph atlas selftest: %d codepoints on a %dx%d atlas, "
              "all inked, metrics intact" % (len(entries), w, h))
        return 0

    if not args.out:
        parser.error("choose an output header or --selftest; generated NOTHING")

    w, h, atlas, entries = build()
    lines = [
        "/* Generated by tools/render_prompt_glyphs.py; do not edit.",
        " *",
        " * The port's OWN prompt-glyph atlas: shared port-assets SVGs",
        " * rasterised at build time. No game font contributed a pixel to this",
        " * file. Design units are font pixels (18px cell); the runtime scales",
        " * them by the text scale the drawer itself uses. */",
        "#ifndef X2_PROMPT_GLYPH_ATLAS_H",
        "#define X2_PROMPT_GLYPH_ATLAS_H",
        "",
        "#include <stdint.h>",
        "",
        f"#define X2_PROMPT_ATLAS_W {w}u",
        f"#define X2_PROMPT_ATLAS_H {h}u",
        "#define X2_PROMPT_CELL_DESIGN 18 /* font pixels */",
        "",
        "struct x2_prompt_cell {",
        "    float u0, v0, u1, v1;   /* bottom-origin V, like the game's */",
        "    int16_t design_w, design_h;",
        "    int16_t advance;        /* design pixels; negative rewinds */",
        "};",
        "",
        f"#define X2_PROMPT_CELL_COUNT {len(entries)}u",
        "static const struct x2_prompt_cell x2_prompt_cells"
        "[X2_PROMPT_CELL_COUNT] = {",
    ]
    for e in entries:
        u0 = e["x"] / w
        u1 = (e["x"] + e["w"]) / w
        v_top = e["y"] / h
        v_bot = (e["y"] + e["h"]) / h
        lines.append(
            "    { %.6ff, %.6ff, %.6ff, %.6ff, %d, %d, %d }, /* %s 0x%02x */"
            % (u0, 1.0 - v_bot, u1, 1.0 - v_top,
               e["design_w"], e["design_h"], e["advance"],
               e["name"], e["code"]))
    lines.append("};")
    lines.append("")
    lines.append(f"#define X2_PROMPT_ATLAS_BYTES {len(atlas)}u")
    lines.append("static const uint8_t x2_prompt_atlas[X2_PROMPT_ATLAS_BYTES] = {")
    for i in range(0, len(atlas), 20):
        chunk = atlas[i:i + 20]
        lines.append("    " + ",".join(str(b) for b in chunk) + ",")
    lines.append("};")
    lines.extend(["", "#endif /* X2_PROMPT_GLYPH_ATLAS_H */", ""])
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text("\n".join(lines), encoding="ascii")
    print(f"generated {args.out}: {len(entries)} cells on {w}x{h}, "
          f"{len(atlas)} bytes")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
