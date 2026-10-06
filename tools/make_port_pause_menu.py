#!/usr/bin/env python3
"""Add the port-owned settings route to a user-supplied retail pause menu.

The pause menu is authored data, not a fixed native array.  Its presentation
IGB reserves button10 through button12 even though the shipped XMLB uses only
button1 through button9.  This tool consumes that reserved capacity: it adds
the model, highlight, and selectable button10 records while leaving every
retail row (including Options and the debug row) intact.

Only the derived XMLB is written into the gitignored X2_ASSETS pack.  Neither
the source menu nor the presentation IGB is modified or copied into git.
"""

from __future__ import annotations

import argparse
from pathlib import Path
import re
import sys


ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))

from alchemy_path import add_alchemy_tools_to_path  # noqa: E402

add_alchemy_tools_to_path()
import xmlb  # noqa: E402


BUTTON = "button10"
COMMAND = "port_settings"
TITLE = "PORT SETTINGS"
RESERVED_NODE_COPIES = 3


class PauseMenuRefusal(ValueError):
    """The source assets do not have the exact extension seam we require."""


def _named(children: list[xmlb.Node], name: str) -> list[xmlb.Node]:
    return [child for child in children if child.get("name") == name]


def _one(children: list[xmlb.Node], name: str) -> xmlb.Node:
    matches = _named(children, name)
    if len(matches) != 1:
        raise PauseMenuRefusal(
            f"pause menu has {len(matches)} {name!r} item(s); expected exactly 1"
        )
    return matches[0]


def _insert_after(children: list[xmlb.Node], anchor: xmlb.Node,
                  item: xmlb.Node) -> None:
    children.insert(children.index(anchor) + 1, item)


def derive_pause_menu(source: bytes, presentation_igb: bytes) -> bytes:
    """Return one extended pause XMLB, or refuse without a partial result."""
    reserved = len(re.findall(rb"(?:^|\x00)button10\x00", presentation_igb))
    if reserved != RESERVED_NODE_COPIES:
        raise PauseMenuRefusal(
            "pause presentation contains "
            f"{reserved} delimited button10 name(s); expected "
            f"{RESERVED_NODE_COPIES} reserved nodes"
        )

    try:
        root = xmlb.parse(source)
    except ValueError as error:
        raise PauseMenuRefusal(f"source is not a complete XMLB: {error}") from error
    if root.name != "MENU" or root.get("type") != "PAUSE_MENU":
        raise PauseMenuRefusal(
            f"source root is {root.name!r}/{root.get('type')!r}; expected MENU/PAUSE_MENU"
        )

    for name in (BUTTON, f"{BUTTON}_back", f"{BUTTON}_highlight"):
        if _named(root.children, name):
            raise PauseMenuRefusal(
                f"pause menu already owns {name!r}; refusing to overwrite a retail row"
            )

    back_anchor = _one(root.children, "button9_back")
    highlight_anchor = _one(root.children, "button9_highlight")
    option_rows = [child for child in root.children
                   if child.get("usecmd") == "openmenu options"]
    if len(option_rows) != 1:
        raise PauseMenuRefusal(
            "pause menu has "
            f"{len(option_rows)} retail Options route(s); expected exactly 1"
        )

    _insert_after(root.children, back_anchor, xmlb.Node("item", [
        ("enabled", "false"), ("name", f"{BUTTON}_back"),
        ("type", "MENU_ITEM_MODEL"),
    ]))
    _insert_after(root.children, highlight_anchor, xmlb.Node("item", [
        ("enabled", "false"), ("name", f"{BUTTON}_highlight"),
        ("type", "MENU_ITEM_MODEL"),
    ]))
    _insert_after(root.children, option_rows[0], xmlb.Node("item", [
        ("focusitemname", f"{BUTTON}_highlight"), ("name", BUTTON),
        ("style", "STYLE_MENU_BLACK"), ("text", TITLE),
        ("usecmd", COMMAND),
    ]))
    return xmlb.serialise(root)


def write_derived_pause_menu(source: Path, presentation_igb: Path,
                             output: Path) -> None:
    derived = derive_pause_menu(source.read_bytes(), presentation_igb.read_bytes())
    output.parent.mkdir(parents=True, exist_ok=True)
    pending = output.with_name(output.name + ".new")
    pending.write_bytes(derived)
    pending.replace(output)


def _synthetic_menu(*, options: bool = True,
                    button10: bool = False) -> bytes:
    root = xmlb.Node("MENU", [("name", "pause"), ("type", "PAUSE_MENU")])
    for suffix in ("_back", "_highlight"):
        for number in range(1, 10):
            root.children.append(xmlb.Node("item", [
                ("enabled", "false"), ("name", f"button{number}{suffix}"),
                ("type", "MENU_ITEM_MODEL"),
            ]))
    for number in range(1, 10):
        attrs = [("focusitemname", f"button{number}_highlight"),
                 ("name", f"button{number}"),
                 ("style", "STYLE_MENU_BLACK")]
        if options and number == 6:
            attrs.extend((("text", "OPTIONS"),
                          ("usecmd", "openmenu options")))
        root.children.append(xmlb.Node("item", attrs))
    if button10:
        root.children.append(xmlb.Node("item", [("name", BUTTON)]))
    return xmlb.serialise(root)


def selftest() -> int:
    failures: list[str] = []
    reserved = b"\x00button10\x00" * RESERVED_NODE_COPIES
    source = _synthetic_menu()
    try:
        root = xmlb.parse(derive_pause_menu(source, reserved))
        expected = {
            BUTTON: (COMMAND, f"{BUTTON}_highlight"),
            f"{BUTTON}_back": (None, None),
            f"{BUTTON}_highlight": (None, None),
        }
        for name, (command, focus) in expected.items():
            matches = _named(root.children, name)
            if len(matches) != 1:
                failures.append(f"positive output has {len(matches)} {name} rows")
                continue
            if command is not None and matches[0].get("usecmd") != command:
                failures.append(f"positive {name} command changed")
            if focus is not None and matches[0].get("focusitemname") != focus:
                failures.append(f"positive {name} focus owner changed")
        retail = [child for child in root.children
                  if child.get("usecmd") == "openmenu options"]
        if len(retail) != 1 or retail[0].get("name") != "button6":
            failures.append("positive output changed the retail Options route")
    except PauseMenuRefusal as error:
        failures.append(f"positive derivation refused: {error}")

    negatives = (
        (source, b"\x00button10\x00" * 2, "missing reserved node"),
        (_synthetic_menu(button10=True), reserved, "existing button10"),
        (_synthetic_menu(options=False), reserved, "missing retail Options"),
        (b"not xmlb", reserved, "malformed XMLB"),
    )
    rejected = 0
    for candidate, igb, label in negatives:
        try:
            derive_pause_menu(candidate, igb)
            failures.append(f"negative {label} was accepted")
        except PauseMenuRefusal:
            rejected += 1

    for failure in failures:
        print(f"FAIL: {failure}", file=sys.stderr)
    total = 5
    passed = total - len(failures)
    print(f"port pause menu: {passed} of {total} checks passed; "
          f"{rejected} of {len(negatives)} invalid inputs refused")
    return 1 if failures else 0


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--selftest", action="store_true")
    parser.add_argument("source", nargs="?", type=Path)
    parser.add_argument("presentation_igb", nargs="?", type=Path)
    parser.add_argument("output", nargs="?", type=Path)
    args = parser.parse_args()
    if args.selftest:
        return selftest()
    if not args.source or not args.presentation_igb or not args.output:
        parser.error("source, presentation_igb and output are required")
    try:
        write_derived_pause_menu(args.source, args.presentation_igb, args.output)
    except (OSError, PauseMenuRefusal) as error:
        print(f"make_port_pause_menu: REFUSING: {error}", file=sys.stderr)
        return 2
    print(f"port pause menu: wrote {args.output}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
