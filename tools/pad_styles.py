#!/usr/bin/env python3
"""PlayStation and Nintendo variants of the prompt glyphs.

The atlas publishes one cell per controller POSITION, named after the Xbox
360 button there (`face_a` is the bottom face button, `lb` the left bumper,
...). A style redraws the cells whose printed label differs on that family and
keeps the shared Xbox art for the ones that look the same everywhere (d-pad,
sticks). Each style becomes a second atlas with the identical layout, so the
runtime switches style by uploading other pixels -- no codepoint, cell or text
layout changes.

Positions, not names: SDL reports every pad positionally, so the bottom face
button of a Nintendo pad is the one printed `B`, and it is drawn here as `B`.

The palette, outline weights and shoulder/trigger silhouettes come from the
shared `draw_xbox360.py`, so the three styles read as one set.
"""

from __future__ import annotations

from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))
from shared_dir import shared_dir                              # noqa: E402

sys.path.insert(0, str(Path(shared_dir("port-assets", marker="sets")) / "tools"))
import draw_xbox360 as xbox                                    # noqa: E402

STYLES = ("playstation", "nintendo")

# ---- letters and digits, in the 72x72 cell's units ------------------------
# The Xbox set's own L and R outlines (from its LB/RB marks), so a shoulder
# label reads in the same lettering on every style.
LETTER_L = "M10 23h7v18h13v7H10Z"
LETTER_R = ("M10 23h14c8 0 13 4 13 10.5 0 4-2 7-5.7 8.8L39 48h-9l-6.4-5H17v5h-7Z"
            "m7 6v8h6c4.5 0 7-1.4 7-4s-2.5-4-7-4Z")
LETTER_WIDTH = {"L": 20, "R": 29}


def letter(name: str, x: float) -> str:
    """One of the shared shoulder letters with its left edge at `x`."""
    shape = LETTER_L if name == "L" else LETTER_R
    return ('<path transform="translate(%g 0)" fill="%s" fill-rule="evenodd" '
            'd="%s"/>' % (x - 10, xbox.DARK, shape))


def stroked(d: str) -> str:
    return ('<path d="%s" fill="none" stroke="%s" stroke-width="7" '
            'stroke-linecap="round" stroke-linejoin="round"/>' % (d, xbox.DARK))


def digit(value: str, x: float) -> str:
    """A 1 or a 2 about 14 units wide with its left edge at `x`, 23..48 high."""
    if value == "1":
        return stroked("M%g 29 l6 -5 v24" % (x + 3))
    return stroked("M%g 29 q1 -5 7 -5 q7 0 7 6.5 q0 4 -6 9 l-8 8.5 h15"
                   % (x + 1))


def letter_z(x: float) -> str:
    return stroked("M%g 25.5 h15 l-15 19 h15" % (x + 2))


def label(*parts: tuple[str, str]) -> str:
    """Lay a short label out centred in the cell: ('L', 'letter'), ('1', 'digit')..."""
    widths = [LETTER_WIDTH[text] if kind == "letter" else 16
              for text, kind in parts]
    gap = 4
    x = 36 - (sum(widths) + gap * (len(parts) - 1)) / 2
    out = []
    for (text, kind), width in zip(parts, widths, strict=True):
        if kind == "letter":
            out.append(letter(text, x))
        elif kind == "digit":
            out.append(digit(text, x))
        else:
            out.append(letter_z(x))
        x += width + gap
    return "".join(out)


def shoulder(name: str, mark: str) -> str:
    """The shared bumper or trigger silhouette with another label on it."""
    if name.endswith("b"):
        shape = ('<rect x="2" y="18" width="68" height="36" rx="16" fill="%s" '
                 'stroke="%s" stroke-width="5"/>' % (xbox.LIGHT, xbox.DARK))
    else:
        shape = ('<path d="M8 18 a10 10 0 0 1 10 -8 h36 a10 10 0 0 1 10 8 '
                 'v14 a22 22 0 0 1 -22 22 h-12 a22 22 0 0 1 -22 -22 z" '
                 'fill="%s" stroke="%s" stroke-width="5" '
                 'stroke-linejoin="round"/>' % (xbox.LIGHT, xbox.DARK))
    return xbox.svg(shape, mark)


def menu_button(*marks: str) -> str:
    return xbox.svg(
        '<circle cx="36" cy="36" r="30" fill="%s" stroke="%s" stroke-width="6"/>'
        % (xbox.LIGHT, xbox.DARK), *marks)


# ---- PlayStation ---------------------------------------------------------
# A light cap and dark outline like every other glyph in the set, carrying the
# symbol in its own colour: the colour and the shape each identify the button.
PS_COLOR = {"cross": "#2F6FD0", "circle": "#D93A3F", "square": "#C259B0",
            "triangle": "#1E9E7A"}
PS_SYMBOL = {
    "cross": "M22 22 L50 50 M50 22 L22 50",
    "circle": "M36 20 a16 16 0 1 0 0.01 0 z",
    "square": "M22 22 h28 v28 h-28 z",
    "triangle": "M36 19 L53 49 H19 z",
}


def ps_face(symbol: str) -> str:
    return xbox.svg(
        '<circle cx="36" cy="36" r="30" fill="%s" stroke="%s" stroke-width="6"/>'
        % (xbox.LIGHT, xbox.DARK),
        '<path d="%s" fill="none" stroke="%s" stroke-width="7" '
        'stroke-linecap="round" stroke-linejoin="round"/>'
        % (PS_SYMBOL[symbol], PS_COLOR[symbol]))


def playstation() -> dict[str, str]:
    return {
        "face_a": ps_face("cross"),
        "face_b": ps_face("circle"),
        "face_x": ps_face("square"),
        "face_y": ps_face("triangle"),
        "lb": shoulder("lb", label(("L", "letter"), ("1", "digit"))),
        "rb": shoulder("rb", label(("R", "letter"), ("1", "digit"))),
        "lt": shoulder("lt", label(("L", "letter"), ("2", "digit"))),
        "rt": shoulder("rt", label(("R", "letter"), ("2", "digit"))),
        # Options: three bars. Share/Create: an arrow leaving a tray.
        "start": menu_button(
            '<g fill="%s"><rect x="20" y="22" width="32" height="7" rx="3"/>'
            '<rect x="20" y="33" width="32" height="7" rx="3"/>'
            '<rect x="20" y="44" width="32" height="7" rx="3"/></g>'
            % xbox.DARK),
        "back": menu_button(
            '<path d="M36 15 l12 13 h-8 v11 h-8 v-11 h-8 z" fill="%s"/>'
            % xbox.DARK,
            stroked("M21 41 v10 h30 v-10")),
    }


# ---- Nintendo ------------------------------------------------------------
# Dark caps with white letters, outlined light like the set's sticks, and the
# letters where Nintendo prints them: A on the right, B at the bottom.
def nintendo_face(printed: str) -> str:
    return xbox.svg(
        '<circle cx="36" cy="36" r="30" fill="%s" stroke="%s" stroke-width="6"/>'
        % (xbox.BODY_DARK, xbox.LIGHT),
        xbox.FACE_MARK[printed])


def nintendo() -> dict[str, str]:
    return {
        "face_a": nintendo_face("b"),     # bottom
        "face_b": nintendo_face("a"),     # right
        "face_x": nintendo_face("y"),     # left
        "face_y": nintendo_face("x"),     # top
        "lb": shoulder("lb", label(("L", "letter"))),
        "rb": shoulder("rb", label(("R", "letter"))),
        "lt": shoulder("lt", label(("Z", "z"), ("L", "letter"))),
        "rt": shoulder("rt", label(("Z", "z"), ("R", "letter"))),
        "start": menu_button(
            '<g fill="%s"><rect x="18" y="32" width="36" height="8" rx="3"/>'
            '<rect x="32" y="18" width="8" height="36" rx="3"/></g>'
            % xbox.DARK),
        "back": menu_button(
            '<rect x="18" y="32" width="36" height="8" rx="3" fill="%s"/>'
            % xbox.DARK),
    }


def style_svgs(style: str) -> dict[str, str]:
    """The cells `style` redraws, by atlas name, as SVG text."""
    if style == "playstation":
        return playstation()
    if style == "nintendo":
        return nintendo()
    raise ValueError(f"unknown pad style {style!r}; expected one of {STYLES}")


def source_paths() -> list[Path]:
    """Files whose content controls the style atlases (for build dependencies)."""
    return [Path(__file__).resolve(), Path(xbox.__file__).resolve()]


if __name__ == "__main__":
    out = ROOT / "scratch" / "pad-styles"
    for name in STYLES:
        folder = out / name
        folder.mkdir(parents=True, exist_ok=True)
        for glyph, body in style_svgs(name).items():
            (folder / f"{glyph}.svg").write_text(body, encoding="ascii")
    print(f"wrote preview SVGs under {out}")
