#!/usr/bin/env python3
"""Put this port's frames beside the control's, as a sheet and as numbers.

The control (`stock` under Wine) is the only configuration that draws the game
correctly, and every rendering question is settled against it. Until now that
was done by opening two files in an image viewer and remembering -- which is
how "the characters are black" and "the models are warped" both went a week
without a denominator, and how one unrepresentative draw came to stand for a
whole frame.

This does two things and refuses to guess at a third:

  * a CONTACT SHEET, ours on top and the control below, so the same scene can
    be looked at side by side rather than in sequence;
  * the same METRICS the oracle cache records for every capture (mean luma, the
    fraction below 16, the fraction above 128), for both sides.

It does NOT compute a pixel difference between the two. The runs are not
frame-synchronised and are driven by different scripts, so their frames show
different moments; a per-pixel diff of two different moments is a number that
looks like evidence and is not. Matching a scene is the human's job, and the
sheet is what makes it possible.

Usage:
  tools/shot_compare.py --ours <glob> --control <glob> [-o out.png] [--cols N]
  tools/shot_compare.py --ours 'scratch/screenshots/postfix.ppm*' \
                        --control 'scratch/oracle/<key>/shots/stock.png*'

Exit codes: 0 wrote a sheet, 2 refused (nothing to compare and it says so).
"""
import argparse
import glob
import os
import sys

try:
    from PIL import Image, ImageDraw
except ImportError:
    sys.exit("shot_compare: needs Pillow (python3 -m pip install --user Pillow)")  # noqa: E501


def refuse(msg):
    """Exit 2 with a reason on stderr -- never a silent or empty comparison."""
    print("shot_compare: " + msg, file=sys.stderr)
    raise SystemExit(2)


def measure(im):
    """Luma statistics, matching tools/oracle.py's `measure` field for field."""
    g = im.convert("L")
    px = list(g.getdata())
    n = len(px)
    if not n:
        return None
    return {
        "mean_luma": sum(px) / float(n),
        "frac_lt16": sum(1 for p in px if p < 16) / float(n),
        "frac_gt128": sum(1 for p in px if p > 128) / float(n),
        "pixels": n,
    }


def load(pattern, label):
    """Every frame matching the pattern, oldest first, as (path, Image).

    A pattern that matches nothing is a REFUSAL, not an empty sheet: a sheet
    with one row is indistinguishable from a comparison whose other side never
    rendered, and that is exactly the mistake this tool exists to stop.
    """
    paths = sorted(p for p in glob.glob(pattern)
                   if not p.endswith((".render.png", ".txt", ".json")))
    if not paths:
        refuse("--%s matched NO files with the pattern %r. Nothing was "
               "compared." % (label, pattern))
    out = []
    for p in paths:
        try:
            im = Image.open(p)
            im.load()
            out.append((p, im.convert("RGB")))
        except Exception as e:                      # noqa: BLE001
            # FAIL FAST. This used to warn and carry on, which produces a
            # sheet with a frame silently missing from one side -- the exact
            # shape of comparison this tool exists to prevent.
            refuse("%s could not be read (%s). A comparison missing a frame is "
                   "not a comparison." % (p, e))
    if not out:
        refuse("every file matching --%s failed to open. Nothing was "
               "compared." % label)
    return out


def report(rows, label):
    print("%s: %d frame(s)" % (label, len(rows)))
    for p, im in rows:
        m = measure(im)
        print("   %-52s %4dx%-4d  mean luma %6.2f  below16 %5.1f%%  "
              "above128 %5.1f%%"
              % (os.path.basename(p), im.width, im.height,
                 m["mean_luma"], m["frac_lt16"] * 100, m["frac_gt128"] * 100))


def sheet(ours, control, out, cols, thumb_w):
    """Ours on top, the control below, each row labelled with its filename."""
    def scale(im):
        h = max(1, int(im.height * thumb_w / float(im.width)))
        return im.resize((thumb_w, h))

    bands = [("OURS (x2native)", ours), ("CONTROL (stock under Wine)", control)]
    thumbs = [(t, [(os.path.basename(p), scale(im)) for p, im in rows])
              for t, rows in bands]
    cell_h = max(im.height for _, rs in thumbs for _, im in rs)
    pad, head, cap = 6, 22, 14
    ncol = min(cols, max(len(rs) for _, rs in thumbs))
    width = ncol * (thumb_w + pad) + pad
    band_h = []
    for _, rs in thumbs:
        nrow = (len(rs) + ncol - 1) // ncol
        band_h.append(head + nrow * (cell_h + cap + pad))
    canvas = Image.new("RGB", (width, sum(band_h) + pad), (24, 24, 28))
    d = ImageDraw.Draw(canvas)
    y = pad
    for (title, rs), bh in zip(thumbs, band_h):
        d.text((pad, y), "%s -- %d frame(s)" % (title, len(rs)),
               fill=(255, 235, 120))
        yy = y + head
        for i, (name, im) in enumerate(rs):
            cx = pad + (i % ncol) * (thumb_w + pad)
            cy = yy + (i // ncol) * (cell_h + cap + pad)
            canvas.paste(im, (cx, cy))
            d.text((cx, cy + im.height + 2), name[:34], fill=(190, 190, 200))
        y += bh
    canvas.save(out)
    return canvas.size


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--ours", required=True, help="glob of this port's frames")
    ap.add_argument("--control", required=True, help="glob of control frames")
    ap.add_argument("-o", "--out", default="scratch/screenshots/compare.png")
    ap.add_argument("--cols", type=int, default=5)
    ap.add_argument("--thumb", type=int, default=320, help="thumbnail width")
    a = ap.parse_args()

    ours = load(a.ours, "ours")
    control = load(a.control, "control")
    report(ours, "OURS (x2native)")
    report(control, "CONTROL (stock)")
    os.makedirs(os.path.dirname(a.out) or ".", exist_ok=True)
    w, h = sheet(ours, control, a.out, a.cols, a.thumb)
    print("\nshot_compare: wrote %s (%dx%d). The two runs are NOT "
          "frame-synchronised -- match the scene by eye before concluding "
          "anything from a pair." % (a.out, w, h))


if __name__ == "__main__":
    main()
