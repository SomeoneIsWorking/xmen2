#!/usr/bin/env python3
"""
Publish THIS PORT'S OWN button glyphs into the PC font, so prompts need no Xbox
disc.

## Why this exists and why it does not use the Xbox art

The engine draws button prompts as ordinary text: one byte per glyph, through
the font it is already using. The obvious source for the art is the Xbox
build's atlas, which carries console button art in a band its metrics never
address -- but using it requires the person playing to own the Xbox build as
well as the PC one, which is not a thing to ask of anyone. So the port draws
its own: `assets/buttons/*.svg`, rasterised here and blitted into the PC
atlas's own empty space.

## Where the glyphs go, and the coordinate that took measuring

x2f_med_pc's atlas is 256x256 RGBA8888 with a 68-row band that has no alpha in
it at all -- room for eleven 18x18 icons several times over.

**`t` is measured from the BOTTOM of the image** (C171): reading the 166 glyph
rects of that atlas with `t` as-is finds 8,315 inked pixels inside them, and
reading it flipped finds 17,944 -- which is every inked pixel the atlas has. A
top-down `t = y0/height` therefore points every glyph at the mirror of the cell
it came from, which is what the earlier Xbox-art tool did. This one converts
explicitly, in one place, and its selftest checks the round trip.

## What it refuses to do

A pack that looks built but draws nothing is the failure mode here, so: a
compressed atlas, a band too small for the icons, a codepoint that already
draws, an icon set that is not the expected size, an atlas whose raw bytes
cannot be located exactly once in the file, and a patch that does not verify
are each REFUSED by name. Nothing is written on any of them.

## Usage

    python3 tools/make_pad_font.py <pc_atlas.igb> <pc_metrics.xmlb> <outdir>
    python3 tools/make_pad_font.py --selftest

The output is an `X2_ASSETS` pack; run the game with `X2_ASSETS=<outdir>`.
"""

import argparse
import os
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "tools"))
sys.path.insert(0, os.path.join(ROOT, "scratch", "ref"))

# xmlb belongs to the Alchemy engine, not to this port -- see tools/alchemy_path.
from alchemy_path import add_alchemy_tools_to_path             # noqa: E402
add_alchemy_tools_to_path()

import xmlb                                                     # noqa: E402
from pad_glyph_manifest import FIRST_CODEPOINT, ICONS            # noqa: E402

PFMT_RGBA_8888_32 = 7
CELL = 18                    # pixels; the shipped glyphs are 16-20 tall
GAP = 2
# ---- the coordinate conversion, in ONE place (C171) -----------------------

def row_to_t(row, height):
    """Decoded image row -> the metrics' t. Measured from the BOTTOM."""
    return (height - row) / float(height)


def t_to_row(t, height):
    return round(height - t * height)


# ---- atlas ---------------------------------------------------------------

def decode_atlas(igb_path):
    """(width, height, rgba, raw_pixel_bytes, pixel_format) of the biggest
    image in the file. The RAW bytes come back too, because publishing is a
    byte patch on the file and the decoded copy cannot be written back."""
    from igblib.igb_format.igb_reader import IGBReader
    from igblib.igb_format.igb_objects import IGBObject
    from igblib.scene_graph.sg_materials import extract_image
    from igblib.utils.image_convert import convert_image_to_rgba

    r = IGBReader(str(igb_path))
    r.read()
    best = None
    for obj in r.objects:
        if isinstance(obj, IGBObject) and obj.is_type(b"igImage"):
            pi = extract_image(r, obj)
            if not (pi and pi.width and pi.pixel_data):
                continue
            if best is None or pi.width * pi.height > best.width * best.height:
                best = pi
    if best is None:
        raise SystemExit("REFUSING: %s holds no decodable igImage, so there is "
                         "no atlas to publish into." % igb_path)
    if best.pixel_format != PFMT_RGBA_8888_32:
        raise SystemExit(
            "REFUSING: %s is pixel format %d, not %d (RGBA8888). Publishing "
            "into it would mean re-encoding, and a wrong re-encode looks like "
            "working art until someone reads the glyphs."
            % (igb_path, best.pixel_format, PFMT_RGBA_8888_32))
    rgba = convert_image_to_rgba(best)
    if rgba is None:
        raise SystemExit("REFUSING: %s decoded to no pixels." % igb_path)
    return best.width, best.height, bytearray(rgba), bytes(best.pixel_data)


def empty_band(rgba, w, h):
    """The longest run of rows with NO alpha anywhere. Returns (first, count).

    Measured rather than assumed: the free space in x2f_med_pc is at the TOP of
    the decoded image and at the BOTTOM in t, and a constant here would be
    wrong for the first other font anyone pointed this at.
    """
    runs, cur = [], None
    for y in range(h):
        blank = not any(rgba[(y * w + x) * 4 + 3] > 8 for x in range(w))
        if blank:
            cur = [y, y] if cur is None else [cur[0], y]
        elif cur is not None:
            runs.append(cur)
            cur = None
    if cur is not None:
        runs.append(cur)
    if not runs:
        return 0, 0
    best = max(runs, key=lambda r: r[1] - r[0])
    return best[0], best[1] - best[0] + 1


# ---- icons ---------------------------------------------------------------

def rasterise(icons_dir, names, size, tmp):
    """SVG -> size x size RGBA, via ImageMagick. A missing icon or a missing
    rasteriser is refused; a pack built from ten of eleven icons would draw a
    blank for one prompt and look like a game bug."""
    out = []
    for n in names:
        src = os.path.join(icons_dir, n + ".svg")
        if not os.path.exists(src):
            raise SystemExit("REFUSING: %s does not exist, so the pack would "
                             "be missing the %s prompt." % (src, n))
        dst = os.path.join(tmp, n + ".rgba")
        cmd = ["magick", "-background", "none", src,
               "-resize", "%dx%d" % (size, size),
               "-depth", "8", "RGBA:" + dst]
        try:
            r = subprocess.run(cmd, capture_output=True)
        except FileNotFoundError:
            raise SystemExit("REFUSING: ImageMagick's `magick` is not on "
                             "PATH, so no icon can be rasterised.") from None
        if r.returncode != 0 or not os.path.exists(dst):
            raise SystemExit("REFUSING: rasterising %s failed: %s"
                             % (src, r.stderr.decode()[:200]))
        px = open(dst, "rb").read()
        if len(px) != size * size * 4:
            raise SystemExit("REFUSING: %s rasterised to %d bytes, not the "
                             "%d a %dx%d RGBA image is."
                             % (src, len(px), size * size * 4, size, size))
        if not any(px[i * 4 + 3] > 8 for i in range(size * size)):
            raise SystemExit("REFUSING: %s rasterised to something completely "
                             "transparent -- it would publish as a blank "
                             "prompt." % src)
        out.append(px)
    return out


def blit(rgba, w, px, size, x0, y0):
    for y in range(size):
        for x in range(size):
            s = (y * size + x) * 4
            d = ((y0 + y) * w + x0 + x) * 4
            rgba[d:d + 4] = px[s:s + 4]


# ---- publishing ----------------------------------------------------------

def patch_igb(igb_path, out_path, old_raw, new_raw):
    """Replace the atlas's pixel bytes in the file, leaving every other byte
    alone. The offset is FOUND rather than computed: the raw block must occur
    exactly once, and anything else is refused."""
    data = open(igb_path, "rb").read()
    first = data.find(old_raw)
    if first < 0:
        raise SystemExit("REFUSING: the atlas's raw pixel bytes are not in "
                         "%s as a contiguous block, so they cannot be patched "
                         "in place." % igb_path)
    if data.find(old_raw, first + 1) >= 0:
        raise SystemExit("REFUSING: the atlas's raw pixel bytes occur more "
                         "than once in %s; patching would change something "
                         "other than the atlas." % igb_path)
    out = data[:first] + new_raw + data[first + len(old_raw):]
    if len(out) != len(data):
        raise SystemExit("REFUSING: the patched image is %d bytes where the "
                         "original was %d." % (len(new_raw), len(old_raw)))
    os.makedirs(os.path.dirname(out_path), exist_ok=True)
    open(out_path, "wb").write(out)
    return first


def build(pc_igb, pc_xmlb, outdir, icons_dir=None, first=None, icons=None):
    """Publish the button art into a copy of the font.

    `first` and `icons` override assets/buttons/glyphs.json. They exist so an
    EXPERIMENT -- "does the game draw a glyph we injected at codepoint X?" --
    can be run into a scratch pack without editing shipped data. The shipping
    path (tools/prepare_native_assets.py) passes neither and so always uses the
    manifest, which keeps its 11-icon invariant.
    """
    first = FIRST_CODEPOINT if first is None else first
    icons = ICONS if icons is None else icons
    icons_dir = icons_dir or os.path.join(ROOT, "assets", "buttons")
    w, h, rgba, raw = decode_atlas(pc_igb)
    before = bytes(rgba)   # kept so the verify can say what actually CHANGED
    y0, rows = empty_band(rgba, w, h)
    print("atlas %dx%d RGBA8888; the largest band with no alpha is rows "
          "%d..%d (%d rows)" % (w, h, y0, y0 + rows - 1, rows))
    need = CELL + GAP
    per_row = (w - GAP) // need
    rows_needed = (len(icons) + per_row - 1) // per_row * need + GAP
    if rows < rows_needed:
        raise SystemExit(
            "REFUSING: %d icon(s) of %dx%d need %d row(s) at %d per row and "
            "the atlas's empty band is %d. Publishing would overwrite art the "
            "font draws." % (len(icons), CELL, CELL, rows_needed, per_row, rows))

    root = xmlb.parse(open(pc_xmlb, "rb").read())
    by_num, used, template = {}, set(), None
    for g in root.children:
        if g.name != "glyph":
            continue
        n = int(g.get("num"))
        by_num[n] = g
        if float(g.get("s2")) > float(g.get("s")) and \
           float(g.get("t2")) > float(g.get("t")):
            used.add(n)
            if template is None:
                template = g
    if template is None:
        raise SystemExit("REFUSING: %s has no glyph with a rect, so there is "
                         "nothing to model the new ones on." % pc_xmlb)
    codes = list(range(first, first + len(icons)))
    clash = [c for c in codes if c in used]
    if clash:
        raise SystemExit("REFUSING: codepoint(s) %s already draw in %s; "
                         "publishing buttons there would overwrite real text."
                         % (clash, pc_xmlb))
    missing = [c for c in codes if c not in by_num]
    if missing:
        raise SystemExit("REFUSING: %s has no glyph entry for codepoint(s) %s, "
                         "so there is nothing to point at the art."
                         % (pc_xmlb, missing))

    scratch_raw = os.path.join(ROOT, "scratch", "raw")
    os.makedirs(scratch_raw, exist_ok=True)
    # The raster files are build intermediates, not durable diagnostics. Keep
    # them in the project's gitignored scratch tree (never /tmp), and have the
    # context manager remove the exact directory it created on every exit.
    with tempfile.TemporaryDirectory(prefix="padfont-", dir=scratch_raw) as tmp:
        art = rasterise(icons_dir, icons, CELL, tmp)
    placed = []
    for i, (code, px) in enumerate(zip(codes, art, strict=True)):
        cx = GAP + (i % per_row) * need
        cy = y0 + GAP + (i // per_row) * need
        blit(rgba, w, px, CELL, cx, cy)
        g = by_num[code]
        # t from the BOTTOM (C171). t is the SMALLER of the two, so the lower
        # image row -- larger y -- gives it.
        g.set("s", repr(cx / float(w)))
        g.set("s2", repr((cx + CELL) / float(w)))
        g.set("t", repr(row_to_t(cy + CELL, h)))
        g.set("t2", repr(row_to_t(cy, h)))
        # PIXELS, not a fraction of the line height. Read off the shipped
        # glyphs: 'A' is width="14" height="13" with a UV rect exactly 14x13
        # pixels of the 256x256 atlas, and horizadvance/baseline are pixels
        # too. An earlier version divided by the font height, which produced
        # height="0.9" -- a glyph the game drew faithfully at nine-tenths of a
        # pixel, i.e. invisibly, at every codepoint and in every font. That
        # looked exactly like "the game refuses to draw our codepoint" and cost
        # a day of chasing the wrong layer.
        g.set("width", str(CELL))
        g.set("height", str(CELL))
        g.set("horizadvance", str(CELL + 1))
        g.set("horizoffset", "0")
        # Letters sit ~2px above the box bottom ('A' 13 tall, baseline 11).
        g.set("baseline", str(CELL - 2))
        placed.append((ICONS[i], code, cx, cy))

    new_raw = bytes(rgba) if len(rgba) == len(raw) else None
    if new_raw is None:
        raise SystemExit(
            "REFUSING: the decoded atlas is %d bytes and the raw block is %d, "
            "so the decode is not a straight copy and writing it back would "
            "corrupt the image." % (len(rgba), len(raw)))

    # The units check, because getting it wrong is invisible on screen: a
    # glyph with a fractional height still draws, just too small to see. Every
    # new glyph must have a height inside the range the font's own glyphs use.
    real_h = sorted(float(by_num[c].get("height")) for c in used
                    if by_num[c].get("height"))
    if real_h:
        lo, hi = real_h[0], real_h[-1]
        for c in codes:
            got = float(by_num[c].get("height"))
            if not (lo <= got <= hi):
                raise SystemExit(
                    "REFUSING: codepoint 0x%02x is being published with "
                    "height=%s, and this font's own glyphs run %g..%g. That is "
                    "a UNIT mismatch -- the game would draw it faithfully at "
                    "the wrong size and it would look like a glyph that never "
                    "renders." % (c, got, lo, hi))
        print("metrics units ok: new glyphs are %g tall, the font's own run "
              "%g..%g" % (float(by_num[codes[0]].get("height")), lo, hi))

    igb_out = os.path.join(outdir, "textures", "fonts",
                           os.path.basename(pc_igb).lower())
    xml_out = os.path.join(outdir, "ui", "fonts",
                           os.path.basename(pc_xmlb).lower())
    at = patch_igb(pc_igb, igb_out, raw, new_raw)
    os.makedirs(os.path.dirname(xml_out), exist_ok=True)
    open(xml_out, "wb").write(xmlb.serialise(root))

    # PROVE the patch: decode what was written and check the cells changed and
    # nothing outside them did. A pack that was written but did not take is the
    # failure this whole tool is trying not to ship.
    w2, h2, rgba2, _ = decode_atlas(igb_out)
    if (w2, h2) != (w, h):
        raise SystemExit("REFUSING: the patched atlas decodes %dx%d, not %dx%d."
                         % (w2, h2, w, h))
    cells = [(cx, cy) for _, _, cx, cy in placed]
    lost = changed = outside = 0
    for y in range(h):
        for x in range(w):
            i = (y * w + x) * 4
            got, want = bytes(rgba2[i:i + 4]), bytes(rgba[i:i + 4])
            if got != want:
                lost += 1                      # written, but did not read back
            if got == bytes(before[i:i + 4]):
                continue
            changed += 1
            if not any(cx <= x < cx + CELL and cy <= y < cy + CELL
                       for cx, cy in cells):
                outside += 1
    if lost:
        raise SystemExit("REFUSING: %d pixel(s) read back differently from "
                         "what was written, so the patch did not survive the "
                         "file." % lost)
    if outside:
        raise SystemExit("REFUSING: %d pixel(s) OUTSIDE the button cells "
                         "changed -- the patch hit something else." % outside)
    if not changed:
        raise SystemExit("REFUSING: NOTHING changed in the atlas. The pack "
                         "would look built and draw the original font.")
    print("patched %s at offset 0x%x; %d icon(s) published, %d pixel(s) differ "
          "from the shipped atlas (of %d in the cells) and every one is inside "
          "a cell" % (igb_out, at, len(placed), changed, len(placed) * CELL * CELL))
    for name, code, cx, cy in placed:
        print("    0x%02x  %-10s cell (%d,%d)  t %.4f..%.4f"
              % (code, name, cx, cy,
                 row_to_t(cy + CELL, h), row_to_t(cy, h)))
    print("asset pack written to %s" % outdir)
    return 0


# ---- selftest ------------------------------------------------------------

def _selftest():
    """The parts that need no game: the coordinate conversion, the band
    finder, the blit, and the two refusals that matter most."""
    fails = []

    # C171's conversion must round-trip, and must NOT be the identity -- an
    # identity here is exactly the bug this tool exists to avoid.
    h = 256
    for row in (0, 18, 68, 188, 256):
        if t_to_row(row_to_t(row, h), h) != row:
            fails.append("row %d does not survive row->t->row" % row)
    if abs(row_to_t(0, h) - 0.0) < 1e-9:
        fails.append("row 0 maps to t 0, which is the TOP-DOWN convention the "
                     "atlas does not use")
    if abs(row_to_t(0, h) - 1.0) > 1e-9:
        fails.append("row 0 should be t 1.0 (the top of the image is the top "
                     "of t), got %f" % row_to_t(0, h))

    # The band finder, on an image with TWO blank runs where the longest is
    # not the first -- "returns the first run it finds" passes a single-run
    # case and would put the icons in the smaller gap here.
    w, hh = 64, 64
    img = bytearray(w * hh * 4)
    for y in range(10, 30):
        for x in range(w):
            img[(y * w + x) * 4 + 3] = 255
    y0, rows = empty_band(img, w, hh)          # blank: 0..9 (10) and 30..63 (34)
    if (y0, rows) != (30, 34):
        fails.append("band finder said rows %d..%d (%d) on an image whose "
                     "longest blank run is 30..63 (34)" % (y0, y0 + rows - 1, rows))
    # ... and it must find NOTHING when there is nothing.
    solid = bytearray(b"\xff" * (w * hh * 4))
    if empty_band(solid, w, hh)[1] != 0:
        fails.append("band finder found a blank band in a fully opaque image")

    # The blit must land where it is told, and nowhere else.
    dst = bytearray(w * hh * 4)
    art = bytes([9, 8, 7, 255]) * (4 * 4)
    blit(dst, w, art, 4, 10, 20)
    if bytes(dst[(20 * w + 10) * 4:(20 * w + 10) * 4 + 4]) != b"\x09\x08\x07\xff":
        fails.append("blit did not write its top-left pixel")
    if any(dst[(20 * w + 9) * 4 + k] for k in range(4)):
        fails.append("blit wrote one pixel to the LEFT of its cell")
    if sum(1 for i in range(w * hh) if dst[i * 4 + 3]) != 16:
        fails.append("blit touched %d pixel(s), not the 16 of a 4x4 cell"
                     % sum(1 for i in range(w * hh) if dst[i * 4 + 3]))

    for f in fails:
        print("FAIL selftest: %s" % f)
    print("make_pad_font selftest: %d of 11 checks passed" % (11 - len(fails)))
    return 1 if fails else 0


def main(argv):
    if len(argv) > 1 and argv[1] == "--selftest":
        return _selftest()
    ap = argparse.ArgumentParser(add_help=False)
    ap.add_argument("igb")
    ap.add_argument("xmlb")
    ap.add_argument("outdir")
    ap.add_argument("--first-codepoint", type=lambda v: int(v, 0), default=None,
                    help="override glyphs.json's first_codepoint; for building "
                         "an experimental pack into scratch")
    ap.add_argument("--icons", default=None,
                    help="comma-separated subset of the icon names, in order")
    ap.add_argument("-h", "--help", action="store_true")
    if len(argv) < 4 or "-h" in argv or "--help" in argv:
        print(__doc__)
        print("usage: make_pad_font.py <pc_atlas.igb> <pc_metrics.xmlb> "
              "<outdir> [--first-codepoint N] [--icons a,b,c]\n"
              "       make_pad_font.py --selftest", file=sys.stderr)
        return 2
    a = ap.parse_args(argv[1:])
    return build(a.igb, a.xmlb, a.outdir, first=a.first_codepoint,
                 icons=a.icons.split(",") if a.icons else None)


if __name__ == "__main__":
    sys.exit(main(sys.argv))
