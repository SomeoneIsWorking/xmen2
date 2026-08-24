#!/usr/bin/env python3
"""
Publish controller and keyboard prompt glyphs into the PC font, with no Xbox
disc or baked key labels.

## Why this exists and why it does not use the Xbox art

The engine draws prompts as ordinary text: one byte per glyph, through the font
it is already using. The obvious source for controller art is the Xbox
build's atlas, which carries console button art in a band its metrics never
address -- but using it requires the person playing to own the Xbox build as
well as the PC one, which is not a thing to ask of anyone. So the port draws
its own through the shared `port-assets` controller set. Keyboard labels reuse
that repo's blank keycap geometry and keep the game's live binding text.

## Where the glyphs go, and the coordinate that took measuring

x2f_med_pc's atlas is 256x256 RGBA8888 with a 68-row band that has no alpha in
it at all -- room for sixteen 18x18 pad icons and four keycap layout glyphs.

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
from io import BytesIO
import os
import sys
import tempfile

from PIL import Image
from resvg_py import svg_to_bytes

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "tools"))
sys.path.insert(0, os.path.join(ROOT, "scratch", "ref"))

# xmlb belongs to the Alchemy engine, not to this port -- see tools/alchemy_path.
from alchemy_path import add_alchemy_tools_to_path             # noqa: E402
add_alchemy_tools_to_path()

import xmlb                                                     # noqa: E402
from pad_glyph_manifest import (FIRST_CODEPOINT, ICONS,          # noqa: E402
                                KEYCAP_FIRST_CODEPOINT,
                                KEYCAP_PARTS, SET_NAME,
                                keycap_svg_path, svg_paths)
import port_assets                                              # noqa: E402


def port_assets_path(name):
    """One icon of the shared set, by name -- for the --icons experiment path."""
    return str(port_assets.path(SET_NAME, name, start=__import__(
        "pathlib").Path(ROOT)))

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

def rasterise(sources, size, tmp):
    """SVG -> size x size RGBA, via the locked resvg Python package.

    A missing icon or failed raster is refused; a pack built from fifteen of
    sixteen icons would draw a blank for one prompt and look like a game bug.

    `sources` are full paths, because the art is NOT in this repo -- it comes
    from the shared `port-assets` set that every port in the tree draws its
    controller from."""
    out = []
    for src in sources:
        n = os.path.splitext(os.path.basename(src))[0]
        if not os.path.exists(src):
            raise SystemExit("REFUSING: %s does not exist, so the pack would "
                             "be missing the %s prompt." % (src, n))
        try:
            png = svg_to_bytes(svg_path=str(src), width=size, height=size)
            with Image.open(BytesIO(png)) as image:
                image.load()
                if image.size != (size, size):
                    raise ValueError("renderer returned %dx%d" % image.size)
                px = image.convert("RGBA").tobytes()
        except Exception as error:
            raise SystemExit("REFUSING: rasterising %s with resvg failed: %s"
                             % (src, error)) from error
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


def blit(rgba, w, px, pw, ph, x0, y0):
    """Copy one RGBA rectangle in, MIRRORED VERTICALLY.

    decode_atlas hands back a BOTTOM-UP buffer while the font's glyph UVs are
    top-down into the real texture. Measured on a stock glyph: reading 'A'
    bottom-up out of the decoded image gives an upside-down A, so the decoded
    rows run the other way from the way the game samples them. Our UVs already
    account for that (row_to_t) and land on the right cell -- but art written
    straight in lands mirrored, which draws an upside-down L on the LB button
    and is invisible on the symmetric ones. So the source rows go in reversed.
    """
    for y in range(ph):
        for x in range(pw):
            s = ((ph - 1 - y) * pw + x) * 4
            d = ((y0 + y) * w + x0 + x) * 4
            rgba[d:d + 4] = px[s:s + 4]


def rasterise_keycap(src, tmp):
    """Shared blank cap -> the three pieces the one-byte font can compose.

    The game's stock letters are bright, whereas the shared SVG's labelled
    example uses dark ink on a light face. This consumer reverses only the
    luminance of the blank geometry so the unchanged stock letters remain
    readable; the shape, bevel, outline and scalable composition still come
    from the shared asset.
    """
    width, height = 45, CELL  # cap_extra_wide is 180:72
    try:
        png = svg_to_bytes(svg_path=str(src), width=width, height=height)
        with Image.open(BytesIO(png)) as image:
            image.load()
            if image.size != (width, height):
                raise ValueError("renderer returned %dx%d" % image.size)
            pixels = bytearray(image.convert("RGBA").tobytes())
    except Exception as error:
        raise SystemExit("REFUSING: rasterising keycap %s with resvg failed: %s"
                         % (src, error)) from error
    if len(pixels) != width * height * 4:
        raise SystemExit("REFUSING: keycap raster has %d bytes, expected %d."
                         % (len(pixels), width * height * 4))
    for i in range(width * height):
        at = i * 4
        if pixels[at + 3] == 0:
            continue
        luminance = (pixels[at] * 54 + pixels[at + 1] * 183
                     + pixels[at + 2] * 19) // 256
        value = max(28, min(225, 238 - luminance))
        pixels[at:at + 3] = bytes((value, value, value))

    def column_slice(x0, piece_width):
        out = bytearray()
        for y in range(height):
            start = (y * width + x0) * 4
            out.extend(pixels[start:start + piece_width * 4])
        return bytes(out)

    # The middle is sampled from the unrounded centre and overlaps at an
    # eight-pixel advance. Repetition therefore stretches without seams and
    # still covers variable-width stock letters.
    return {
        "left": (5, column_slice(0, 5)),
        "middle": (12, column_slice(17, 12)),
        "right": (5, column_slice(width - 5, 5)),
    }


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


def build(pc_igb, pc_xmlb, outdir, first=None, icons=None):
    """Publish the button art into a copy of the font.

    `first` and `icons` override assets/buttons/glyphs.json. They exist so an
    EXPERIMENT -- "does the game draw a glyph we injected at codepoint X?" --
    can be run into a scratch pack without editing shipped data. The shipping
    path (tools/prepare_native_assets.py) passes neither and so always uses the
    manifest, which keeps its 16-icon invariant.

    The ART comes from the shared `port-assets` set the manifest names, not
    from this repo; `svg_paths()` is the one place that resolves it.
    """
    shipping = icons is None and first is None
    first = FIRST_CODEPOINT if first is None else first
    sources = (svg_paths() if icons is None
               else [port_assets_path(n) for n in icons])
    icons = ICONS if icons is None else icons
    w, h, rgba, raw = decode_atlas(pc_igb)
    before = bytes(rgba)   # kept so the verify can say what actually CHANGED
    y0, rows = empty_band(rgba, w, h)
    print("atlas %dx%d RGBA8888; the largest band with no alpha is rows "
          "%d..%d (%d rows)" % (w, h, y0, y0 + rows - 1, rows))
    need = CELL + GAP
    per_row = (w - GAP) // need
    icon_rows = (len(icons) + per_row - 1) // per_row
    rows_needed = icon_rows * need + GAP + (need if shipping else 0)
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
    # The font's own baseline, by majority of the glyphs that draw. A single
    # template glyph is not enough: the first one with a rect turned out to
    # have baseline="1".
    tally = {}
    for c in used:
        b = by_num[c].get("baseline")
        if b is not None:
            tally[b] = tally.get(b, 0) + 1
    if not tally:
        raise SystemExit("REFUSING: no glyph in %s carries a baseline, so the "
                         "new ones have nothing to line up with." % pc_xmlb)
    font_baseline = max(tally, key=lambda k: tally[k])
    print("font baseline %s (%d of %d drawing glyphs agree)"
          % (font_baseline, tally[font_baseline], len(used)))

    codes = list(range(first, first + len(icons)))
    keycap_codes = (list(range(KEYCAP_FIRST_CODEPOINT,
                               KEYCAP_FIRST_CODEPOINT + len(KEYCAP_PARTS)))
                    if shipping else [])
    all_codes = codes + keycap_codes
    clash = [c for c in all_codes if c in used]
    if clash:
        raise SystemExit("REFUSING: codepoint(s) %s already draw in %s; "
                         "publishing buttons there would overwrite real text."
                         % (clash, pc_xmlb))
    missing = [c for c in all_codes if c not in by_num]
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
        art = rasterise(sources, CELL, tmp)
        keycap_art = rasterise_keycap(keycap_svg_path(), tmp) if shipping else {}
    placed = []
    for i, (code, px) in enumerate(zip(codes, art, strict=True)):
        cx = GAP + (i % per_row) * need
        cy = y0 + GAP + (i // per_row) * need
        blit(rgba, w, px, CELL, CELL, cx, cy)
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
        # `baseline` is NOT per-glyph geometry: every glyph in this font uses
        # the same value (11 here) whatever its height, so it is the ascent --
        # the distance from the glyph box TOP down to the text baseline. Deriving
        # CELL-2 from the letters' height/baseline relationship put our glyph
        # five pixels above the line. Take the font's own value instead.
        g.set("baseline", str(font_baseline))
        placed.append((ICONS[i], code, cx, cy, CELL, CELL))

    if shipping:
        key_y = y0 + GAP + icon_rows * need
        key_x = GAP
        metrics = {
            "left": (5, 4),
            "middle": (12, 8),
            "rewind": (0, -8),
            "right": (5, 4),
        }
        for index, name in enumerate(KEYCAP_PARTS):
            code = KEYCAP_FIRST_CODEPOINT + index
            width, advance = metrics[name]
            g = by_num[code]
            if width:
                px_width, px = keycap_art[name]
                if px_width != width:
                    raise SystemExit("REFUSING: keycap %s raster width %d does "
                                     "not match its metric width %d."
                                     % (name, px_width, width))
                blit(rgba, w, px, width, CELL, key_x, key_y)
                g.set("s", repr(key_x / float(w)))
                g.set("s2", repr((key_x + width) / float(w)))
                g.set("t", repr(row_to_t(key_y + CELL, h)))
                g.set("t2", repr(row_to_t(key_y, h)))
                g.set("width", str(width))
                g.set("height", str(CELL))
                g.set("horizoffset", "0")
                g.set("baseline", str(font_baseline))
                placed.append(("keycap_" + name, code, key_x, key_y,
                               width, CELL))
                key_x += width + GAP
            else:
                # Rewind is intentionally invisible. Its negative advance
                # returns the pen over the middle strips so the stock letters
                # draw on top of the background already emitted.
                g.set("s", "0")
                g.set("s2", "0")
                g.set("t", "0")
                g.set("t2", "0")
                g.set("width", "0")
                g.set("height", "0")
                g.set("horizoffset", "0")
                g.set("baseline", str(font_baseline))
            g.set("horizadvance", str(advance))

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
        drawing_codes = [code for _, code, _, _, _, _ in placed]
        for c in drawing_codes:
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
    cells = [(cx, cy, pw, ph) for _, _, cx, cy, pw, ph in placed]
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
            if not any(cx <= x < cx + pw and cy <= y < cy + ph
                       for cx, cy, pw, ph in cells):
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
          "from the shipped atlas and every one is inside a published cell"
          % (igb_out, at, len(placed), changed))
    for name, code, cx, cy, _pw, ph in placed:
        print("    0x%02x  %-10s cell (%d,%d)  t %.4f..%.4f"
              % (code, name, cx, cy,
                 row_to_t(cy + ph, h), row_to_t(cy, h)))
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
    # Rows that DIFFER, so the vertical mirror is visible to this test. The
    # previous version used a uniform cell and could not have told an upright
    # blit from a flipped one -- which is exactly the defect that shipped.
    art = bytearray()
    for row in range(4):
        art += bytes([row, 8, 7, 255]) * 4
    art = bytes(art)
    blit(dst, w, art, 4, 4, 10, 20)
    top = bytes(dst[(20 * w + 10) * 4:(20 * w + 10) * 4 + 4])
    bot = bytes(dst[(23 * w + 10) * 4:(23 * w + 10) * 4 + 4])
    if top != b"\x03\x08\x07\xff":
        fails.append("blit did not MIRROR: its top destination row is %r, "
                     "expected the source's LAST row" % (top,))
    if bot != b"\x00\x08\x07\xff":
        fails.append("blit did not MIRROR: its bottom destination row is %r, "
                     "expected the source's FIRST row" % (bot,))
    if any(dst[(20 * w + 9) * 4 + k] for k in range(4)):
        fails.append("blit wrote one pixel to the LEFT of its cell")
    if sum(1 for i in range(w * hh) if dst[i * 4 + 3]) != 16:
        fails.append("blit touched %d pixel(s), not the 16 of a 4x4 cell"
                     % sum(1 for i in range(w * hh) if dst[i * 4 + 3]))

    # Exercise the shipping resvg path in both directions. Importing the
    # package proves only that the old ModuleNotFoundError is gone; rendering
    # one opaque SVG and refusing both blank and invalid inputs proves the
    # locked dependency is usable and its output gate actually fires.
    scratch = os.path.join(ROOT, "scratch", "raw")
    os.makedirs(scratch, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="pad-font-resvg-", dir=scratch) as tmp:
        opaque = os.path.join(tmp, "opaque.svg")
        transparent = os.path.join(tmp, "transparent.svg")
        invalid = os.path.join(tmp, "invalid.svg")
        open(opaque, "w").write(
            '<svg xmlns="http://www.w3.org/2000/svg" width="8" height="8">'
            '<rect width="8" height="8" fill="#ff0000"/></svg>')
        open(transparent, "w").write(
            '<svg xmlns="http://www.w3.org/2000/svg" width="8" height="8"/>')
        open(invalid, "w").write("not an svg")
        try:
            rendered = rasterise([opaque], 8, tmp)
            if len(rendered) != 1 or len(rendered[0]) != 8 * 8 * 4:
                fails.append("resvg positive control did not produce one 8x8 RGBA image")
        except SystemExit as error:
            fails.append("resvg positive control refused: %s" % error)
        for path, label in ((transparent, "transparent"), (invalid, "invalid")):
            try:
                rasterise([path], 8, tmp)
            except SystemExit:
                continue
            fails.append("resvg %s negative control was accepted" % label)

    for f in fails:
        print("FAIL selftest: %s" % f)
    print("make_pad_font selftest: %d failure(s)" % len(fails))
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
