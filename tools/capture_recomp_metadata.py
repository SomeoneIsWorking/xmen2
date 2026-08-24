#!/usr/bin/env python3
"""Commit-safe capture of Ghidra exports: retain structure, drop code bytes."""

from __future__ import annotations

import argparse
import json
from pathlib import Path
import re
import tempfile


ROOT = Path(__file__).resolve().parent.parent


def modules_from_cmake() -> list[str]:
    text = (ROOT / "CMakeLists.txt").read_text()
    match = re.search(r"set\(X2_MODULES(.*?)\)", text, re.DOTALL)
    if not match:
        raise SystemExit("capture: CMakeLists.txt has no X2_MODULES list")
    return match.group(1).split()


def strip_export(source: Path, destination: Path) -> tuple[int, int]:
    document = json.loads(source.read_text())
    total = stripped = 0
    for function in document.get("functions", []):
        for instruction in function.get("ins", []):
            total += 1
            if instruction.pop("b", None) is not None:
                stripped += 1
    if total == 0:
        raise SystemExit(f"capture: {source} holds 0 instructions")
    destination.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.NamedTemporaryFile(
        "w", encoding="utf-8", dir=destination.parent,
        prefix=f".{destination.name}-", delete=False,
    ) as temporary:
        temporary_path = Path(temporary.name)
        json.dump(document, temporary, separators=(",", ":"))
        temporary.write("\n")
    temporary_path.replace(destination)
    encoded = sum(
        1
        for function in json.loads(destination.read_text()).get("functions", [])
        for instruction in function.get("ins", [])
        if "b" in instruction
    )
    if encoded:
        raise SystemExit(f"capture: {destination} still has {encoded} encoding(s)")
    return total, stripped


def selftest() -> int:
    root = ROOT / "scratch" / "capture-metadata-selftest"
    root.mkdir(parents=True, exist_ok=True)
    source = root / "input.json"
    output = root / "output.json"
    source.write_text('{"functions":[{"ins":[{"a":1,"n":1,"b":"90"}]}]}\n')
    total, stripped = strip_export(source, output)
    clean = '"b"' not in output.read_text()
    print(f"capture metadata selftest: total={total} stripped={stripped} clean={clean}")
    return 0 if (total, stripped, clean) == (1, 1, True) else 1


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--selftest", action="store_true")
    parser.add_argument("--source", type=Path, default=ROOT / "scratch/recomp")
    parser.add_argument("--output", type=Path, default=ROOT / "re/ghidra")
    args = parser.parse_args()
    if args.selftest:
        return selftest()
    instructions = stripped = 0
    for module in modules_from_cmake():
        source = args.source / f"{module}.json"
        if not source.is_file():
            raise SystemExit(f"capture: missing {source}; captured nothing")
        count, dropped = strip_export(source, args.output / source.name)
        instructions += count
        stripped += dropped
        print(f"capture: {module}: {count} instruction(s), {dropped} encoding(s) dropped")
    print(f"capture: {len(modules_from_cmake())} module(s), {instructions} instruction(s), "
          f"{stripped} encoding(s) removed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
