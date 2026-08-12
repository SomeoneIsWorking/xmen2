#!/usr/bin/env python3
"""
Build the Xbox-button font pack: the Xbox atlas, plus metrics that can REACH
its button art.

## Why this exists

The Xbox medium font's atlas carries the console's button art -- d-pad, A, B,
X, Y, black, white, both triggers, both sticks -- in a band the font's own
glyph table does not cover (the highest `t2` in any shipped metric variant is
y=199; the band starts at y=201). So substituting the Xbox font alone changes
nothing on screen, which is exactly what a run measured: Xbox font in place, a
pad connected, caption unchanged. The art is present and unreachable.

This tool makes it reachable. Of the 256 glyphs in the PC metrics, 90 have an
empty rect -- codepoints the font never draws. The button cells are measured
out of the atlas and written into unused `num` slots, so the game's ordinary
text path can draw a button by being handed one byte. No renderer work, no new
draw call, no second asset format.

## The cells are MEASURED, not hardcoded

The rects come from segmenting the atlas by alpha and comparing against the PC
atlas, every run. A table of constants in this file would rot silently the
first time anyone pointed it at a different font, and would give no signal that
it had. If the measurement finds a different number of cells than expected the
tool REFUSES rather than emitting a pack with glyphs pointing at empty space.

## Usage

    python3 tools/make_button_font.py <xbox.igb> <pc.igb> <pc.xmlb> <outdir>
    python3 tools/make_button_font.py --selftest

The output is an `X2_ASSETS` pack:

    <outdir>/textures/fonts/x2f_med_pc.igb    the Xbox atlas, copied
    <outdir>/ui/fonts/x2f_med_pc.xmlb         PC metrics + the button glyphs

Run the game with `X2_ASSETS=<outdir>` and the redirect report names both.
"""

import os
import shutil
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import xmlb                                                     # noqa: E402
import extract_font_igb                                         # noqa: E402

# Where the button glyphs are published. 0x80..0x8a are in the empty-rect set
# of the shipped PC metrics and are contiguous, which keeps the override in
# dinput/the exe a single base + index rather than a lookup table.
FIRST_CODEPOINT = 0x80

# What the band should contain. Order is left-to-right, top row then bottom,
# which is the order the segmentation returns.
EXPECTED_CELLS = 11


def _largest_image(igb_path):
    """The atlas: the biggest decoded image in the file. `extract` returns every
    igImage, and the fonts carry small ones (1x1 up to 64x64) alongside."""
    images = extract_font_igb.extract(igb_path)
    if not images:
        raise SystemExit("REFUSING: %s decoded to NO images. The atlas could "
                         "not be read, so nothing can be measured from it."
                         % igb_path)
    best = max(images, key=lambda im: im[0] * im[1])
    return best                                     # (w, h, rgba, fmt)


def _alpha_rows(rgba, w, h):
    return [any(rgba[(y * w + x) * 4 + 3] > 8 for x in range(w)) for y in range(h)]


def measure_button_cells(xb_igb, pc_igb, first_row=0):
    """Find the cells present in the Xbox atlas and absent from the PC one.

    `first_row` is where the FONT stops -- the atlas above it is the glyph grid,
    which segments into a handful of full-width bands and tells us nothing. The
    caller derives it from the metrics rather than picking a number, so pointing
    this at a differently-packed font moves the boundary with it.

    Returns [(x0, y0, x1, y1)] in reading order, and the denominators needed to
    read a negative: how many bands were scanned and how many cells were seen
    before the PC comparison threw any away.
    """
    xb = _largest_image(xb_igb)
    pc = _largest_image(pc_igb)
    xw, xh, xr = xb[0], xb[1], xb[2]
    pw, ph, pr = pc[0], pc[1], pc[2]
    if (xw, xh) != (pw, ph):
        raise SystemExit("REFUSING: atlases are different sizes (%dx%d vs "
                         "%dx%d); the cells cannot be compared cell for cell."
                         % (xw, xh, pw, ph))

    def px(buf, x, y):
        i = (y * xw + x) * 4
        return buf[i:i + 4]

    # Rows that hold anything at all, grouped into bands.
    occupied = _alpha_rows(xr, xw, xh)
    for y in range(min(first_row, xh)):
        occupied[y] = False
    bands, start = [], None
    for y, o in enumerate(occupied + [False]):
        if o and start is None:
            start = y
        elif not o and start is not None:
            bands.append((start, y))
            start = None

    # Recursive X-Y cut. A single column pass is not enough: the two icon rows
    # touch in places, so cutting by columns once merges them into one 110px
    # blob. Alternating column and row cuts until neither finds a gap is the
    # standard answer and needs no threshold -- a gap is a fully transparent
    # line, not "mostly" transparent. It also keeps a letter inside a circle
    # with its circle, which connected components would have split.
    def occupied_cols(x0, x1, y0, y1):
        return [any(px(xr, x, y)[3] > 8 for y in range(y0, y1))
                for x in range(x0, x1)]

    def occupied_rows(x0, x1, y0, y1):
        return [any(px(xr, x, y)[3] > 8 for x in range(x0, x1))
                for y in range(y0, y1)]

    def runs(flags, base):
        out, st = [], None
        for i, f in enumerate(list(flags) + [False]):
            if f and st is None:
                st = i
            elif not f and st is not None:
                out.append((base + st, base + i))
                st = None
        return out

    def cut(x0, y0, x1, y1, by_col, depth=0):
        if depth > 16 or x1 <= x0 or y1 <= y0:
            return [(x0, y0, x1, y1)]
        if by_col:
            parts = runs(occupied_cols(x0, x1, y0, y1), x0)
            boxes = [(a, y0, b, y1) for a, b in parts]
        else:
            parts = runs(occupied_rows(x0, x1, y0, y1), y0)
            boxes = [(x0, a, x1, b) for a, b in parts]
        if len(boxes) == 1 and boxes[0] == (x0, y0, x1, y1):
            # this axis found no gap; try the other, and stop if neither does
            return ([(x0, y0, x1, y1)] if depth and not by_col
                    else cut(x0, y0, x1, y1, not by_col, depth + 1))
        out = []
        for b in boxes:
            out.extend(cut(b[0], b[1], b[2], b[3], not by_col, depth + 1))
        return out

    cells, seen_total = [], 0
    for y0, y1 in bands:
        for x0, cy0, x1, cy1 in cut(0, y0, xw, y1, True):
            seen_total += 1
            # "Xbox-only" means the PC atlas is EMPTY here -- every pixel
            # transparent -- while the Xbox one draws something. Comparing
            # bytes instead would also flag cells that merely re-encoded: the
            # colour bar at the foot of this atlas differs in 13 of 720 pixels
            # by one step of red, which is compression noise and not art. An
            # "over N% of pixels differ" rule would work too and would be a
            # threshold nobody could justify; "the PC has nothing there" needs
            # no number.
            pc_empty = all(px(pr, x, y)[3] <= 8
                           for x in range(x0, x1) for y in range(cy0, cy1))
            if pc_empty:
                cells.append((x0, cy0, x1, cy1))
    return cells, len(bands), seen_total, (xw, xh)


def first_free_row(root, atlas_h):
    """The first atlas row no glyph reaches. Everything below it is space the
    font does not address, which is where the button art sits."""
    lo = 0.0
    for g in root.children:
        if g.name == "glyph" and float(g.get("t2")) > float(g.get("t")):
            lo = max(lo, float(g.get("t2")))
    return int(lo * atlas_h + 0.5)


def build(xb_igb, pc_igb, pc_xmlb, outdir):
    root = xmlb.parse(open(pc_xmlb, "rb").read())
    aw, ah = _largest_image(pc_igb)[:2]
    row = first_free_row(root, ah)
    print("metrics %s: glyphs reach atlas row %d of %d; scanning below it"
          % (os.path.basename(pc_xmlb), row, ah))
    cells, nbands, nseen, (aw, ah) = measure_button_cells(xb_igb, pc_igb, row)
    print("atlas %dx%d: %d occupied band(s) below row %d, %d cell(s) segmented, "
          "%d differ from the PC atlas"
          % (aw, ah, nbands, row, nseen, len(cells)))
    if len(cells) != EXPECTED_CELLS:
        raise SystemExit(
            "REFUSING: expected %d Xbox-only cells (d-pad, A, B, X, Y, black, "
            "white, 2 triggers, 2 sticks) and measured %d. Emitting a pack now "
            "would publish glyphs pointing at the wrong pixels, and it would "
            "look like it worked. Inspect the atlas before changing "
            "EXPECTED_CELLS." % (EXPECTED_CELLS, len(cells)))

    used = set()
    template = None
    for g in root.children:
        if g.name != "glyph":
            continue
        num = int(g.get("num"))
        s, s2 = float(g.get("s")), float(g.get("s2"))
        t, t2 = float(g.get("t")), float(g.get("t2"))
        if s2 > s and t2 > t:
            used.add(num)
            if template is None:
                template = g
    if template is None:
        raise SystemExit("REFUSING: %s has no glyph with a non-empty rect, so "
                         "there is nothing to model the new ones on." % pc_xmlb)

    codes = list(range(FIRST_CODEPOINT, FIRST_CODEPOINT + len(cells)))
    clash = [c for c in codes if c in used]
    if clash:
        raise SystemExit("REFUSING: codepoint(s) %s already draw a glyph in "
                         "%s; publishing buttons there would overwrite real "
                         "text." % (clash, pc_xmlb))

    height = float(root.get("height", "20"))
    by_num = {}
    for g in root.children:
        if g.name == "glyph":
            by_num[int(g.get("num"))] = g

    for code, (x0, y0, x1, y1) in zip(codes, cells):
        g = by_num[code]
        w, h = x1 - x0, y1 - y0
        g.set("s", repr(x0 / aw))
        g.set("s2", repr(x1 / aw))
        g.set("t", repr(y0 / ah))
        g.set("t2", repr(y1 / ah))
        # width/height are in the same units the shipped glyphs use: a fraction
        # of the em, taken from the atlas pixels over the font's own height.
        g.set("width", repr(w / height))
        g.set("height", repr(h / height))
        g.set("horizadvance", repr((w + 2) / height))
        g.set("horizoffset", "0")
        g.set("baseline", template.get("baseline"))
        print("  U+%04X <- atlas x%d-%d y%d-%d (%dx%d)"
              % (code, x0, x1, y0, y1, w, h))

    tex = os.path.join(outdir, "textures", "fonts")
    ui = os.path.join(outdir, "ui", "fonts")
    os.makedirs(tex, exist_ok=True)
    os.makedirs(ui, exist_ok=True)
    shutil.copyfile(xb_igb, os.path.join(tex, "x2f_med_pc.igb"))
    out_xmlb = os.path.join(ui, "x2f_med_pc.xmlb")
    blob = xmlb.serialise(root)
    open(out_xmlb, "wb").write(blob)
    print("wrote %s (%d bytes) and %s (%d bytes)"
          % (os.path.join(tex, "x2f_med_pc.igb"), os.path.getsize(xb_igb),
             out_xmlb, len(blob)))
    print("run with X2_ASSETS=%s" % outdir)
    return codes


def _selftest():
    """Prove the pieces fire, on synthetic data, without needing the game.

    The case fed in MUST produce a positive: an atlas with one cell the other
    does not have. A self-test that only checked "no crash" would pass on a
    measurement function that always returned nothing.
    """
    w = h = 16
    pc = bytearray(w * h * 4)
    xb = bytearray(w * h * 4)
    for y in range(4, 9):                    # one 5x5 opaque cell, Xbox only
        for x in range(3, 8):
            i = (y * w + x) * 4
            xb[i:i + 4] = b"\xff\x00\x00\xff"
    saved = extract_font_igb.extract
    try:
        extract_font_igb.extract = lambda p: [(
            w, h, bytes(xb if "xb" in p else pc), 15)]
        cells, nbands, nseen, dims = measure_button_cells("xb.igb", "pc.igb")
    finally:
        extract_font_igb.extract = saved
    assert dims == (w, h), dims
    assert cells == [(3, 4, 8, 9)], cells
    print("selftest: measurement found the planted cell %s "
          "(%d band, %d segmented) -- it can return a POSITIVE"
          % (cells[0], nbands, nseen))

    # ...and the negative: identical atlases must yield nothing, and must say
    # what they scanned rather than just going quiet.
    try:
        extract_font_igb.extract = lambda p: [(w, h, bytes(xb), 15)]
        cells, nbands, nseen, _ = measure_button_cells("xb.igb", "pc.igb")
    finally:
        extract_font_igb.extract = saved
    assert cells == [], cells
    assert nseen == 1, nseen
    print("selftest: identical atlases -> 0 of %d segmented cell(s) differ "
          "-- a negative that carries its denominator" % nseen)

    root = xmlb.parse(struct.pack("<2I", xmlb.MAGIC, 1)
                      + struct.pack("<4I", 24, xmlb.NONE, xmlb.NONE, 0)
                      + b"R\0")
    assert root.name == "R" and not root.children
    assert xmlb.serialise(root)[:8] == struct.pack("<2I", xmlb.MAGIC, 1)
    print("selftest: xmlb round-trips a minimal document")
    print("make_button_font selftest: 3 of 3 checks passed")
    return 0


def main(argv):
    if len(argv) > 1 and argv[1] == "--selftest":
        return _selftest()
    if len(argv) != 5:
        print(__doc__, file=sys.stderr)
        return 2
    build(argv[1], argv[2], argv[3], argv[4])
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
