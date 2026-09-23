#!/usr/bin/env python3
"""Rasterise the shared prompt SVGs into a PORT-OWNED atlas header.

    python3 tools/render_prompt_glyphs.py <out-header>
    python3 tools/render_prompt_glyphs.py --selftest

This is the build-time half of the renderer-side prompt feature: the game's
fonts stay untouched, and the port draws its own controller/keycap art at the
text-renderer override (src/native/prompt_glyph_draw.c). The art comes from
the shared `port-assets` sets named by assets/buttons/glyphs.json -- the same
manifest and the recovered metric semantics (18px design cell, advances
19 / 4 / 4), so the label composition in
prompt_labels.c keeps its exact meaning. What changes is WHO owns the pixels:
a generated header in this repo, never a patched copy of a shipped font.

A keyboard key is drawn whole from the shared keyboard set: the blank cap as a
three-slice frame whose straight middle stretches to the width the game's text
layout reserved, and the binding's name lettered at runtime in the set's own
typeface (src/native/keycap_labels.c), because the game localizes key names.
This header carries the frame and the set's label metrics; no pixel of a key
comes from a game font.

The atlas is rasterised at 4x supersample so a glyph stays crisp when the
runtime draws it larger than 18 design pixels (the AUTO text scale is 2.64 at
2160p on this install).
"""

from __future__ import annotations

import argparse
from io import BytesIO
import os
from pathlib import Path
import shlex
import subprocess
import sys
import tempfile
from xml.etree import ElementTree

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))

from pad_glyph_manifest import (ICON_CODEPOINTS, ICONS,          # noqa: E402
                                KEYCAP_CODEPOINTS, KEYCAP_PARTS,
                                RETAIL_FONT_CODEPOINTS, draw_keyboard,
                                keycap_svg_path, shared_source_paths,
                                svg_paths)

from PIL import Image                                            # noqa: E402
from resvg_py import svg_to_bytes                                # noqa: E402

# Design units are FONT PIXELS: the shipped pack's cell and advances, which
# prompt_labels.c's composition was verified against (#91).  The shared SVG
# raster remains an 18px source cell, but controller art uses the whole 19px
# footprint already reserved by its advance.  Keeping those two concepts in
# one CELL constant made the native quad unnecessarily smaller without buying
# any layout spacing: the pen already moves by PAD_ADVANCE.  Keycap edges
# keep their recovered widths: they carry LAYOUT ONLY, the margin either side
# of the binding's name, and draw nothing themselves.  At run time a font's
# design pixel is its capital height / SOURCE_CELL (prompt_glyph_metrics.c),
# so 18 design pixels are exactly the capitals of the text beside a prompt.
SOURCE_CELL = 18
PAD_ADVANCE = SOURCE_CELL + 1
PAD_DRAW_SIZE = PAD_ADVANCE
KEYCAP_METRICS = {"left": (5, 4), "right": (5, 4)}
SS = 4                       # raster supersample per design pixel
# The shared cap's box height in its own units; rasterised SOURCE_CELL design
# pixels tall at SS, one unit is one atlas pixel.
CAP_UNITS = draw_keyboard().CAP_UNITS
if CAP_UNITS != SOURCE_CELL * SS:
    raise SystemExit(f"REFUSING: the shared cap is {CAP_UNITS} units tall; the "
                     f"frame slices assume one unit per atlas pixel "
                     f"({SOURCE_CELL * SS})")
# The frame's slices, in cap units: each rounded end (its 5-unit inset, 10-unit
# radius and half the 5-unit stroke fit in 16) and one straight column of the
# middle, which is what stretches.
CAP_EDGE_UNITS = 16
# How much taller than the capitals beside it a key's cap stands, so its
# letters sit inside a key rather than the key inside the line of text.  The
# cell is then sized from the rows the rasterised cap actually covers.
KEYCAP_BODY_OVER_CAPS = 1.25
CAP_MIDDLE_UNITS = 4
KEYCAP_FRAME = ("left", "middle", "right")

ATLAS_W = 512


def rasterise_icon(src: Path) -> tuple[bytes, int, int]:
    """One icon -> RGBA bytes at SOURCE_CELL*SS, plus its draw box."""
    return (rasterise_svg(None, src, SOURCE_CELL * SS, str(src)),
            PAD_DRAW_SIZE, PAD_DRAW_SIZE)


def rasterise_svg(svg: str | None, path: Path | None, width: int,
                  what: str) -> bytes:
    """One SVG -> RGBA bytes, `width` x SOURCE_CELL*SS; refuses a blank."""
    height = SOURCE_CELL * SS
    try:
        png = svg_to_bytes(svg_string=svg, svg_path=str(path) if path else None,
                           width=width, height=height)
        with Image.open(BytesIO(png)) as image:
            image.load()
            if image.size != (width, height):
                raise ValueError("renderer returned %dx%d" % image.size)
            pixels = image.convert("RGBA").tobytes()
    except Exception as error:
        raise SystemExit(f"REFUSING: rasterising {what} failed: {error}") from error
    if not any(pixels[at + 3] > 8 for at in range(0, len(pixels), 4)):
        raise SystemExit(f"REFUSING: {what} rasterised fully transparent -- "
                         "it would publish a blank prompt.")
    return pixels


def columns(pixels: bytes, width: int, x0: int, w: int) -> bytes:
    """Columns x0..x0+w of a `width`-wide, SOURCE_CELL*SS-tall RGBA raster."""
    out = bytearray()
    for y in range(SOURCE_CELL * SS):
        start = (y * width + x0) * 4
        out.extend(pixels[start:start + w * 4])
    return bytes(out)


def rasterise_keycap_frame(src: Path) -> dict[str, bytes]:
    """The shared blank cap -> its left end, one middle column, its right end.

    Drawn in the set's own colours: the letters over it are the set's too, so
    nothing has to be inverted for a game font to read on it."""
    height = SOURCE_CELL * SS
    root = ElementTree.parse(src).getroot()
    units = float(root.get("viewBox", "0 0 0 0").split()[2])
    width = round(units * height / CAP_UNITS)
    if width < 2 * CAP_EDGE_UNITS + CAP_MIDDLE_UNITS:
        raise SystemExit(f"REFUSING: keycap {src} is {units:g} units wide, too "
                         "narrow for two rounded ends and a straight middle")
    pixels = rasterise_svg(None, src, width, f"keycap {src}")
    middle = (width - CAP_MIDDLE_UNITS) // 2
    frame = {"left": columns(pixels, width, 0, CAP_EDGE_UNITS),
             "middle": columns(pixels, width, middle, CAP_MIDDLE_UNITS),
             "right": columns(pixels, width, width - CAP_EDGE_UNITS,
                              CAP_EDGE_UNITS)}
    for name, piece in frame.items():
        if not any(piece[at + 3] > 8 for at in range(0, len(piece), 4)):
            raise SystemExit(f"REFUSING: keycap {src} {name!r} slice "
                             "rasterised fully transparent -- it would "
                             "publish a blank prompt piece.")
    return frame


def keycap_cell_height(middle: bytes) -> int:
    """The key cell's design height, from the rows the cap's body covers."""
    rows = SOURCE_CELL * SS
    stride = CAP_MIDDLE_UNITS * 4
    covered = [y for y in range(rows)
               if any(middle[y * stride + at + 3] > 8
                      for at in range(0, stride, 4))]
    body = covered[-1] - covered[0] + 1
    return round(SOURCE_CELL * KEYCAP_BODY_OVER_CAPS * rows / body)


def build() -> tuple[int, int, bytes, list[dict], dict[str, list[dict]]]:
    """Atlas pixels + one entry per published codepoint, in codepoint order."""
    icons = [rasterise_icon(p) for p in svg_paths()]
    frame = rasterise_keycap_frame(keycap_svg_path())

    cell = SOURCE_CELL * SS
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

    for i, (px, design_w, design_h) in enumerate(icons):
        ax, ay = place(cell, cell)
        entry = {"name": ICONS[i], "code": ICON_CODEPOINTS[i],
                 "x": ax, "y": ay, "w": cell, "h": cell,
                 "design_w": design_w, "design_h": design_h,
                 "advance": PAD_ADVANCE}
        entries.append(entry)
        cells.append((entry, px, cell, cell))
    key_h = keycap_cell_height(frame["middle"])
    for index, name in enumerate(KEYCAP_PARTS):
        # Layout only: the key is drawn whole over the span these reserve.
        design_w, advance = KEYCAP_METRICS[name]
        entries.append({"name": "keycap_" + name,
                        "code": KEYCAP_CODEPOINTS[index],
                        "x": 0, "y": 0, "w": 0, "h": 0,
                        "design_w": design_w, "design_h": key_h,
                        "advance": advance})
    extras: dict[str, list[dict]] = {"frame": []}
    for name in KEYCAP_FRAME:
        w = CAP_EDGE_UNITS if name != "middle" else CAP_MIDDLE_UNITS
        ax, ay = place(w, cell)
        entry = {"name": "frame_" + name, "x": ax, "y": ay, "w": w, "h": cell,
                 "design_w": w / SS}
        extras["frame"].append(entry)
        cells.append((entry, frame[name], w, cell))

    atlas_h = y + row_h
    atlas = bytearray(ATLAS_W * atlas_h * 4)
    for entry, px, aw, ah in cells:
        for row in range(ah):
            src_at = (row * aw) * 4
            dst_at = ((entry["y"] + row) * ATLAS_W + entry["x"]) * 4
            atlas[dst_at:dst_at + aw * 4] = px[src_at:src_at + aw * 4]
    return ATLAS_W, atlas_h, bytes(atlas), entries, extras


def with_gaps(entries: list[dict]) -> list[dict]:
    """One cell per byte of the published run; skipped retail bytes are empty."""
    by_code = {e["code"]: e for e in entries}
    first, last = entries[0]["code"], entries[-1]["code"]
    return [by_code.get(code, {"name": "retail font", "code": code,
                               "published": False})
            for code in range(first, last + 1)]


def emit_header(out: Path) -> tuple[int, int, int]:
    """Write the one production header and return its atlas summary."""
    w, h, atlas, entries, extras = build()
    entries = with_gaps(entries)
    keyboard = draw_keyboard()

    def uv(e: dict) -> str:
        return "%.6ff, %.6ff, %.6ff, %.6ff" % (
            e["x"] / w, 1.0 - (e["y"] + e["h"]) / h,
            (e["x"] + e["w"]) / w, 1.0 - e["y"] / h)

    lines = [
        "/* Generated by tools/render_prompt_glyphs.py; do not edit.",
        " *",
        " * The port's OWN prompt-glyph atlas: shared port-assets SVGs",
        " * rasterised at build time. No game font contributed a pixel to this",
        " * file. Design units are font pixels (18px source cell, 19px pad",
        " * draw box); the runtime scales",
        " * them by the text scale the drawer itself uses. */",
        "#ifndef X2_PROMPT_GLYPH_ATLAS_H",
        "#define X2_PROMPT_GLYPH_ATLAS_H",
        "",
        "#include <stdint.h>",
        "",
        f"#define X2_PROMPT_ATLAS_W {w}u",
        f"#define X2_PROMPT_ATLAS_H {h}u",
        "#define X2_PROMPT_SOURCE_CELL_DESIGN 18 /* font pixels */",
        "#define X2_PROMPT_PAD_DRAW_DESIGN 19 /* font pixels */",
        "",
        "struct x2_prompt_cell {",
        "    float u0, v0, u1, v1;   /* bottom-origin V, like the game's */",
        "    int16_t design_w, design_h;",
        "    int16_t advance;        /* design pixels; negative rewinds */",
        "    uint8_t published;      /* 0: a retail font owns this byte */",
        "};",
        "",
        "/* Keyboard-key art drawn whole over a composed keycap's span: its",
        "   sheet, UVs, and design width at the 18px source height. A label's",
        "   cell keeps the cap's full height so its letters sit on the cap's",
        "   baseline. */",
        "enum { X2_KEYCAP_SHEET_ATLAS, X2_KEYCAP_SHEET_LABELS };",
        "struct x2_keycap_art {",
        "    float u0, v0, u1, v1;   /* bottom-origin V */",
        "    float design_w;",
        "    uint8_t sheet;",
        "};",
        "enum { X2_KEYCAP_FRAME_LEFT, X2_KEYCAP_FRAME_MIDDLE, "
        "X2_KEYCAP_FRAME_RIGHT, X2_KEYCAP_FRAME_COUNT };",
        "",
        "/* The shared keyboard set's label, in units of its cap box, and the",
        "   raster scale: a cap is X2_PROMPT_SUPERSAMPLE pixels per design",
        "   pixel. From port-assets tools/draw_keyboard.py. */",
        f"#define X2_PROMPT_SUPERSAMPLE {SS}",
        f"#define X2_KEYCAP_CAP_UNITS {keyboard.CAP_UNITS}",
        f"#define X2_KEYCAP_LABEL_SIZE {keyboard.LABEL_SIZE}",
        f"#define X2_KEYCAP_LABEL_BASELINE {keyboard.LABEL_BASELINE}",
        f"#define X2_KEYCAP_LABEL_MARGIN {keyboard.LABEL_MARGIN}",
        f"#define X2_KEYCAP_INK 0x{keyboard.INK.lstrip('#')}u /* RGB */",
        "",
        f"#define X2_PROMPT_CELL_COUNT {len(entries)}u",
        f"#define X2_PROMPT_ATLAS_BYTES {len(atlas)}u",
        "#ifdef X2_PROMPT_GLYPH_ATLAS_DEFINE",
        "const struct x2_prompt_cell x2_prompt_cells"
        "[X2_PROMPT_CELL_COUNT] = {",
    ]
    for e in entries:
        if not e.get("published", True):
            lines.append("    { 0.0f, 0.0f, 0.0f, 0.0f, 0, 0, 0, 0 }, /* %s 0x%02x */"
                         % (e["name"], e["code"]))
            continue
        u0 = e["x"] / w
        u1 = (e["x"] + e["w"]) / w
        v_top = e["y"] / h
        v_bot = (e["y"] + e["h"]) / h
        lines.append(
            "    { %.6ff, %.6ff, %.6ff, %.6ff, %d, %d, %d, 1 }, /* %s 0x%02x */"
            % (u0, 1.0 - v_bot, u1, 1.0 - v_top,
               e["design_w"], e["design_h"], e["advance"],
               e["name"], e["code"]))
    lines.append("};")
    lines.append("const struct x2_keycap_art "
                 "x2_keycap_frame[X2_KEYCAP_FRAME_COUNT] = {")
    for e in extras["frame"]:
        lines.append("    { %s, %.4ff, X2_KEYCAP_SHEET_ATLAS }, /* %s */"
                     % (uv(e), e["design_w"], e["name"]))
    lines.append("};")
    lines.append("")
    lines.append("const uint8_t x2_prompt_atlas[X2_PROMPT_ATLAS_BYTES] = {")
    for i in range(0, len(atlas), 20):
        chunk = atlas[i:i + 20]
        lines.append("    " + ",".join(str(b) for b in chunk) + ",")
    lines.append("};")
    lines.extend([
        "#else",
        "extern const struct x2_prompt_cell "
        "x2_prompt_cells[X2_PROMPT_CELL_COUNT];",
        "extern const struct x2_keycap_art "
        "x2_keycap_frame[X2_KEYCAP_FRAME_COUNT];",

        "extern const uint8_t x2_prompt_atlas[X2_PROMPT_ATLAS_BYTES];",
        "#endif",
        "",
        "#endif /* X2_PROMPT_GLYPH_ATLAS_H */",
        "",
    ])
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_text("\n".join(lines), encoding="ascii")
    print(f"generated {out}: {len(entries)} cells on {w}x{h}, "
          f"{len(atlas)} bytes")
    return w, h, len(entries)


def selftest(compiler: str | None) -> int:
    """Exercise rasterisation and the generated header's two-TU C contract."""
    paths = svg_paths()
    if len(paths) != len(ICONS) or len(rasterise_icon(paths[0])[0]) == 0:
        raise SystemExit("pad glyph atlas selftest: icon set broken")
    frame = rasterise_keycap_frame(keycap_svg_path())
    if set(frame) != set(KEYCAP_FRAME):
        raise SystemExit("pad glyph atlas selftest: keycap slicing broken")
    w, h, atlas, entries, extras = build()
    if not any(atlas[i * 4 + 3] for i in range(len(atlas) // 4)):
        raise SystemExit("pad glyph atlas selftest: atlas has no ink")
    by_name = {e["name"]: e for e in entries}
    if [e["name"] for e in extras["frame"]] != \
            ["frame_" + name for name in KEYCAP_FRAME]:
        raise SystemExit("pad glyph atlas selftest: the keycap frame is not "
                         "its three slices, in order")
    if by_name["keycap_left"]["w"] or by_name["keycap_right"]["w"]:
        raise SystemExit("pad glyph atlas selftest: a keycap edge draws "
                         "pixels of its own; the key is drawn whole")
    face_a = by_name["face_a"]
    if face_a["advance"] != PAD_ADVANCE:
        raise SystemExit("pad glyph atlas selftest: pad advance changed")
    if (face_a["design_w"], face_a["design_h"]) != \
            (PAD_DRAW_SIZE, PAD_DRAW_SIZE):
        raise SystemExit("pad glyph atlas selftest: pad art no longer fills "
                         "its reserved advance footprint")
    # The cell follows the rows the cap covers: a cap filling every row needs
    # only the body's own height, and the shipped cap, inset, needs more.
    full = bytes([255]) * (CAP_MIDDLE_UNITS * 4 * SOURCE_CELL * SS)
    if keycap_cell_height(full) != round(SOURCE_CELL * KEYCAP_BODY_OVER_CAPS):
        raise SystemExit("pad glyph atlas selftest: a full-height cap was not "
                         "sized to the body alone")
    if by_name["keycap_left"]["design_h"] <= keycap_cell_height(full):
        raise SystemExit("pad glyph atlas selftest: the shipped cap's inset "
                         "rows did not enlarge its cell")

    raw = ROOT / "scratch" / "raw"
    raw.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="prompt-atlas-selftest-", dir=raw) as tmp:
        work = Path(tmp)
        blank_middle = work / "blank-middle.svg"
        blank_middle.write_text(
            '<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 180 72" '
            'width="180" height="72">'
            '<rect x="0" y="0" width="16" height="72" fill="#fff"/>'
            '<rect x="164" y="0" width="16" height="72" fill="#fff"/>'
            "</svg>\n",
            encoding="ascii",
        )
        try:
            rasterise_keycap_frame(blank_middle)
        except SystemExit as error:
            if "'middle' slice rasterised fully transparent" not in str(error):
                raise
        else:
            raise SystemExit(
                "pad glyph atlas selftest: transparent keycap slice was accepted"
            )
        header = work / "prompt_glyph_atlas.h"
        generated_w, generated_h, generated_count = emit_header(header)
        cells = with_gaps(entries)
        if (generated_w, generated_h, generated_count) != (w, h, len(cells)):
            raise SystemExit("pad glyph atlas selftest: emitted summary changed")
        gaps = [c["code"] for c in cells if not c.get("published", True)]
        if gaps != [code for code in RETAIL_FONT_CODEPOINTS
                    if cells[0]["code"] <= code <= cells[-1]["code"]] \
                or len(cells) != len(entries) + len(gaps):
            raise SystemExit("pad glyph atlas selftest: retail-font bytes are "
                             "not exactly the unpublished cells")
        (work / "define.c").write_text(
            "#define X2_PROMPT_GLYPH_ATLAS_DEFINE\n"
            '#include "prompt_glyph_atlas.h"\n',
            encoding="ascii",
        )
        (work / "extern.c").write_text(
            '#include "prompt_glyph_atlas.h"\n'
            "int main(void) {\n"
            "    volatile uint8_t atlas_byte = x2_prompt_atlas[0];\n"
            "    (void)atlas_byte;\n"
            "    return x2_prompt_cells[0].design_w == "
            "X2_PROMPT_PAD_DRAW_DESIGN ? 0 : 1;\n"
            "}\n",
            encoding="ascii",
        )
        compiler_argv = shlex.split(compiler or os.environ.get("CC", "cc"))
        if not compiler_argv:
            raise SystemExit("pad glyph atlas selftest: empty C compiler command")
        result = subprocess.run(
            [*compiler_argv, "-std=c11", "-I", str(work),
             str(work / "define.c"), str(work / "extern.c"),
             "-o", str(work / "header-contract")],
            text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
            check=False,
        )
        if result.returncode:
            raise SystemExit(
                "pad glyph atlas selftest: define/extern header compile failed:\n"
                + result.stdout
            )
        run = subprocess.run(
            [str(work / "header-contract")], text=True,
            stdout=subprocess.PIPE, stderr=subprocess.STDOUT, check=False,
        )
        if run.returncode:
            raise SystemExit(
                "pad glyph atlas selftest: define/extern linked check failed:\n"
                + run.stdout
            )
    print("pad glyph atlas selftest: %d codepoints on a %dx%d atlas, "
          "all slices inked, metrics and define/extern contract intact"
          % (len(entries), w, h))
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--selftest", action="store_true")
    parser.add_argument("--print-inputs", action="store_true")
    parser.add_argument("--cc", help="C compiler command for --selftest")
    parser.add_argument("out", nargs="?", type=Path)
    args = parser.parse_args()

    choices = int(args.selftest) + int(args.print_inputs) + int(args.out is not None)
    if choices != 1:
        parser.error("choose exactly one output header, --selftest, or --print-inputs")
    if args.cc and not args.selftest:
        parser.error("--cc is only valid with --selftest")
    if args.print_inputs:
        for path in shared_source_paths():
            print(path.resolve())
        return 0
    if args.selftest:
        return selftest(args.cc)
    emit_header(args.out)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
