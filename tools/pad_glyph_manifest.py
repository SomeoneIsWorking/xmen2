#!/usr/bin/env python3
"""Validate the prompt-glyph manifest and generate its C codepoint header.

`assets/buttons/glyphs.json` is this PORT's decision: which prompt glyphs it
publishes and at which private text codepoints. **The art itself is not this
port's** -- it lives in `shared/port-assets`, consumed rather than vendored,
because every port in the tree was redrawing the same controller. The manifest
names a set and the glyphs it wants out of it; `port_assets` resolves the
checkout and refuses by naming every path it tried.

The ORDER of `icons` is the codepoint order and is load-bearing: the generated
`X2_PAD_GLYPH_*` macros and the native RGBA atlas layout both come from it.
Codepoints are assigned from `first_codepoint` upward, icons then keycap parts,
skipping every byte in `retail_font_codepoints`: a byte a shipped font already
draws is never private, and publishing one of ours there silently turned the
whole glyph off at runtime (0x8C, the Windows-1252 OE, disabled d-pad up; 0x99,
the trademark sign, and 0x9C, oe, disabled every keycap -- #184). Those three
are the only bytes of 0x80..0x9F any of the four retail font records draws
(measured over every record the game offers); from 0xA0 up the Latin-1 letters
are dense, so the run must end below it.
"""

from __future__ import annotations

import argparse
import json
from pathlib import Path
import shutil
import sys


ROOT = Path(__file__).resolve().parent.parent
MANIFEST = ROOT / "assets" / "buttons" / "glyphs.json"

sys.path.insert(0, str(ROOT / "tools"))
from shared_dir import shared_dir                              # noqa: E402

sys.path.insert(0, shared_dir("port-assets", marker="sets"))
import port_assets                                             # noqa: E402


def load() -> tuple[int, str, list[str], str, str, list[str], list[int]]:
    data = json.loads(MANIFEST.read_text())
    first = data.get("first_codepoint")
    reserved = data.get("retail_font_codepoints")
    icons = data.get("icons")
    set_name = data.get("set")
    keyboard_set = data.get("keyboard_set")
    keyboard_source = data.get("keyboard_source")
    keycap_parts = data.get("keycap_parts")
    if not isinstance(first, int) or not isinstance(icons, list) \
            or not isinstance(set_name, str) \
            or not isinstance(keyboard_set, str) \
            or not isinstance(keyboard_source, str) \
            or not isinstance(keycap_parts, list) \
            or not isinstance(reserved, list) \
            or not all(isinstance(code, int) for code in reserved):
        raise ValueError("glyphs.json needs an integer first_codepoint, a "
                         "string set, a list of icons, keyboard set/source/parts and a "
                         "list of integer retail_font_codepoints")
    # A fixed count, not a minimum: a manifest that lost an entry must be
    # refused, and the number is meant to be changed deliberately when the set
    # this port publishes grows. It grew from 14 to 16 when the authored
    # stick-click glyphs were wired into the physical-code path (#90), and to
    # 24 when the stick DIRECTIONS were, which menu footers bind (#184).
    if len(icons) != 24 or len(set(icons)) != len(icons):
        raise ValueError(f"needs 24 unique icon names, found {len(icons)} / "
                         f"{len(set(icons))}")
    # Every name must exist in the SHARED set. This is the check that catches a
    # rename over there before it publishes a blank prompt here.
    available = set(port_assets.names(set_name, start=ROOT))
    missing = [name for name in icons if name not in available]
    if missing:
        raise ValueError(f"the shared set {set_name!r} has no glyph(s) "
                         f"{missing!r}. It has: {', '.join(sorted(available))}")
    keyboard_available = set(port_assets.names(keyboard_set, start=ROOT))
    if keyboard_source not in keyboard_available:
        raise ValueError(f"the shared set {keyboard_set!r} has no glyph "
                         f"{keyboard_source!r}. It has: "
                         f"{', '.join(sorted(keyboard_available))}")
    expected_parts = ["left", "right"]
    if keycap_parts != expected_parts:
        raise ValueError(f"keycap_parts must be {expected_parts!r}, found "
                         f"{keycap_parts!r}; their metrics are semantic")
    if first < 0 or first + len(icons) + len(keycap_parts) + len(reserved) > 256:
        raise ValueError("glyph codepoints do not fit in the game's one-byte text path")
    return (first, set_name, icons, keyboard_set, keyboard_source,
            keycap_parts, reserved)


# The first byte past the Windows-1252 punctuation area; see the module doc.
PRIVATE_END = 0xA0


def assign_codepoints(first: int, count: int, reserved: list[int]) -> list[int]:
    """`count` codepoints from `first` upward, skipping every reserved byte."""
    codes: list[int] = []
    code = first
    while len(codes) < count:
        if code not in reserved:
            codes.append(code)
        code += 1
    return codes


(FIRST_CODEPOINT, SET_NAME, ICONS, KEYBOARD_SET, KEYBOARD_SOURCE,
 KEYCAP_PARTS, RETAIL_FONT_CODEPOINTS) = load()
_CODEPOINTS = assign_codepoints(FIRST_CODEPOINT, len(ICONS) + len(KEYCAP_PARTS),
                                RETAIL_FONT_CODEPOINTS)
ICON_CODEPOINTS = _CODEPOINTS[:len(ICONS)]
KEYCAP_CODEPOINTS = _CODEPOINTS[len(ICONS):]
LAST_CODEPOINT = _CODEPOINTS[-1]
if LAST_CODEPOINT >= PRIVATE_END:
    raise ValueError(f"glyph codepoints run to 0x{LAST_CODEPOINT:02x}, into the "
                     f"Latin-1 letters every retail font draws from "
                     f"0x{PRIVATE_END:02x}")


def svg_paths() -> list[Path]:
    """Where the art actually is, in codepoint order."""
    return [port_assets.path(SET_NAME, name, start=ROOT) for name in ICONS]


def keycap_svg_path() -> Path:
    """Blank shared keycap; this port stretches its straight middle."""
    return port_assets.path(KEYBOARD_SET, KEYBOARD_SOURCE, start=ROOT)


def key_font_path() -> Path:
    """The shared keyboard set's typeface; the runtime letters keys in it."""
    return port_assets.key_font(start=ROOT)


def draw_keyboard():
    """The shared set's authoring module: it owns the key typography."""
    import importlib.util
    path = keyboard_tool_path()
    spec = importlib.util.spec_from_file_location("port_assets_draw_keyboard",
                                                  path)
    if spec is None or spec.loader is None:
        raise SystemExit(f"prompt atlas: cannot load {path}")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    if not hasattr(module, "LABEL_SIZE"):
        raise SystemExit(f"prompt atlas: {path} has no LABEL_SIZE; the pinned "
                         "port-assets predates its shipped key typeface")
    return module


def keyboard_tool_path() -> Path:
    return port_assets.set_dir(KEYBOARD_SET, start=ROOT).parent.parent / \
        "tools" / "draw_keyboard.py"


def shared_source_paths() -> list[Path]:
    """Every shared file whose content or declaration controls this atlas."""
    paths = [
        Path(port_assets.__file__).resolve(),
        port_assets.set_dir(SET_NAME, start=ROOT) / "set.json",
        port_assets.set_dir(KEYBOARD_SET, start=ROOT) / "set.json",
        *svg_paths(),
        keycap_svg_path(),
        keyboard_tool_path(),
    ]
    missing = [path for path in paths if not path.is_file()]
    if missing:
        raise SystemExit(
            "prompt atlas shared dependencies vanished: "
            + ", ".join(str(path) for path in missing)
        )
    if len(paths) != len(set(paths)):
        raise SystemExit("prompt atlas shared dependencies contain duplicate paths")
    return paths


def emit_header(path: Path) -> None:
    lines = ["/* Generated by tools/pad_glyph_manifest.py; do not edit. */",
             "#ifndef X2_PAD_GLYPH_CODES_H", "#define X2_PAD_GLYPH_CODES_H", ""]
    for name, code in zip(ICONS, ICON_CODEPOINTS, strict=True):
        macro = "X2_PAD_GLYPH_" + name.upper()
        lines.append(f"#define {macro:<27} 0x{code:02X}u")
    lines.append("")
    for name, code in zip(KEYCAP_PARTS, KEYCAP_CODEPOINTS, strict=True):
        macro = "X2_KEYCAP_GLYPH_" + name.upper()
        lines.append(f"#define {macro:<27} 0x{code:02X}u")
    # The bounds of the published run, so a "is this byte one of ours" test
    # names the run rather than naming whichever glyph happens to sit at its
    # end -- that test read `<= X2_PAD_GLYPH_DPAD` and would have silently
    # stopped covering the set the moment anything was appended after it.
    lines.append("")
    lines.append(f"#define {'X2_PAD_GLYPH_FIRST':<27} 0x{ICON_CODEPOINTS[0]:02X}u")
    lines.append(f"#define {'X2_PAD_GLYPH_LAST':<27} 0x{ICON_CODEPOINTS[-1]:02X}u")
    # The WHOLE published run, keycap parts included: the renderer override
    # classifies strings against every codepoint this port publishes, and a
    # pad-only bound there would miss the caps silently. The run contains the
    # retail-font bytes it skips; the atlas marks those cells unpublished.
    published_last = LAST_CODEPOINT
    lines.append(f"#define {'X2_PROMPT_GLYPH_FIRST':<27} "
                 f"0x{FIRST_CODEPOINT:02X}u")
    lines.append(f"#define {'X2_PROMPT_GLYPH_LAST':<27} "
                 f"0x{published_last:02X}u")
    lines.extend(["", "#endif /* X2_PAD_GLYPH_CODES_H */", ""])
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text("\n".join(lines), encoding="ascii")
    print(f"generated {path} with {len(ICONS)} pad and {len(KEYCAP_PARTS)} "
          f"keycap glyph codepoint(s) from shared assets")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--selftest", action="store_true")
    parser.add_argument("--emit-header", type=Path)
    parser.add_argument("--print-key-font", action="store_true",
                        help="print the shared key typeface's path")
    args = parser.parse_args()
    if args.selftest:
        where = port_assets.set_dir(SET_NAME, start=ROOT)
        print(f"pad glyph manifest: {len(ICONS)} glyph(s) from {SET_NAME!r} at "
              f"{where}, bytes 0x{ICON_CODEPOINTS[0]:02x}.."
              f"0x{LAST_CODEPOINT:02x} skipping "
              f"{[hex(code) for code in RETAIL_FONT_CODEPOINTS]}")
        for path in svg_paths():
            if not path.is_file():
                raise SystemExit(f"pad glyph manifest: {path} vanished between "
                                 f"resolution and use")
        cap = keycap_svg_path()
        if not cap.is_file():
            raise SystemExit(f"pad glyph manifest: {cap} vanished between "
                             "resolution and use")
        print(f"pad glyph manifest: all {len(ICONS)} pad SVG(s) and shared "
              f"keycap {cap.name} readable")
        return 0
    if args.print_key_font:
        print(key_font_path().resolve())
        return 0
    if args.emit_header:
        emit_header(args.emit_header)
        return 0
    parser.error("choose --selftest, --emit-header or "
                 "--print-key-font; generated NOTHING")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
