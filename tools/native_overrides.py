#!/usr/bin/env python3
"""Which native overrides this repository registers, read from the source.

The authority is the call itself: an override exists because some translation
unit calls ``x86_register_override(module, ep, fn)``, and nothing generates
those any more. Scanning the source is therefore reading the registry, not
approximating it.

It is a REFUSING scan. A source tree with no registrations at all, or a file
that mentions the function without a readable call, is reported rather than
returned as an empty result -- an empty answer from a wiring checker reads
exactly like "the wiring is fine".
"""

from __future__ import annotations

from pathlib import Path
import re
import sys
from typing import Iterator, NamedTuple


CALL = re.compile(
    r"x86_register_override\s*\(\s*\"([^\"]+)\"\s*,\s*"
    r"([0-9A-Za-z_]+)\s*,\s*"
    r"([A-Za-z_][A-Za-z0-9_]*)\s*\)", re.S)

# An entry point is as often a named constant as a literal, and the name is the
# point -- LINKED_FONT_LOADER says what the address is for. Only object-like
# defines of an integer in the SAME file are resolved: a value assembled from
# another header would be a guess, and the scan refuses rather than guessing.
DEFINE = re.compile(
    r"^[ \t]*#[ \t]*define[ \t]+([A-Za-z_][A-Za-z0-9_]*)[ \t]+"
    r"\(?\s*(0[xX][0-9a-fA-F]+|\d+)[uUlL]*\s*\)?[ \t]*$", re.M)

# ... and as often an enumerator, which is the better C: `enum { CHUD_DRAW =
# 0x005a43d0u };` gives the address a type as well as a name. Same rule -- an
# integer literal in the same file, nothing computed.
ENUMERATOR = re.compile(
    r"\b([A-Za-z_][A-Za-z0-9_]*)\s*=\s*(0[xX][0-9a-fA-F]+|\d+)[uUlL]*\s*[,}]")

# Mentions that are not registrations: the declaration, the definition, and the
# prose about it. Counted so the scan can tell "no call here" from "a call this
# regex could not read", which is the failure that would make it silent.
MENTION = re.compile(r"x86_register_override")


class Override(NamedTuple):
    module: str
    ep: int
    path: Path
    fn: str


class ScanError(Exception):
    pass


def _sources(root: Path) -> list[Path]:
    return sorted(p for p in (root / "src").rglob("*.c") if p.is_file())


def scan_overrides(root: Path | str) -> Iterator[Override]:
    """Every (module, ep, path, fn) the source registers, in file order."""
    root = Path(root)
    found: list[Override] = []
    unreadable: list[str] = []
    for path in _sources(root):
        text = path.read_text(errors="replace")
        mentions = len(MENTION.findall(text))
        defines = {name: int(value, 0) for name, value in DEFINE.findall(text)}
        defines.update({name: int(value, 0)
                        for name, value in ENUMERATOR.findall(text)})
        calls = []
        for module, ep, fn in CALL.findall(text):
            token = re.sub(r"[uUlL]+$", "", ep)
            if re.fullmatch(r"0[xX][0-9a-fA-F]+|\d+", token):
                value = int(token, 0)
            elif token in defines:
                value = defines[token]
            else:
                continue          # counted as unreadable by the check below
            calls.append(ep)
            found.append(Override(module, value, path, fn))
        # The declaration in x86rt_native.h is not a .c file, so any mention in
        # a source that is not a readable call is worth naming: a registration
        # built from a macro or a variable would otherwise vanish silently.
        if mentions > len(calls) and path.name not in {"x86rt_native.c"}:
            unreadable.append(f"{path.relative_to(root)} "
                              f"({mentions} mention(s), {len(calls)} readable)")
    if not found:
        raise ScanError(
            f"no x86_register_override call found anywhere under {root}/src. "
            "That is not an empty registry -- it is a scan that stopped "
            "working, and every wiring check built on it would pass.")
    if unreadable:
        raise ScanError(
            "x86_register_override is mentioned where this scan cannot read a "
            "call, so the registry it returns is incomplete: "
            + "; ".join(unreadable))
    return iter(found)


def main(argv: list[str]) -> int:
    root = Path(argv[1]) if len(argv) > 1 else Path(__file__).resolve().parents[1]
    try:
        overrides = list(scan_overrides(root))
    except ScanError as exc:
        print(f"native_overrides: {exc}", file=sys.stderr)
        return 1
    for o in overrides:
        print(f"{o.module} 0x{o.ep:08x} {o.fn} "
              f"{o.path.relative_to(Path(root))}")
    print(f"{len(overrides)} override(s) registered")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
