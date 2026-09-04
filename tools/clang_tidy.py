#!/usr/bin/env python3
"""Run clang-tidy over every first-party translation unit in a CMake build."""

from __future__ import annotations

import argparse
from concurrent.futures import ThreadPoolExecutor
import json
from pathlib import Path
import shutil
import subprocess
import sys


ROOT = Path(__file__).resolve().parents[1]
FIRST_PARTY_ROOTS = (ROOT / "src", ROOT / "tests")
EXCLUDED_ROOTS = (ROOT / "src" / "gen",)


def translation_units(database: Path) -> tuple[Path, ...]:
    records = json.loads(database.read_text(encoding="utf-8"))
    units: set[Path] = set()
    for record in records:
        source = Path(record["file"])
        if not source.is_absolute():
            source = Path(record["directory"]) / source
        source = source.resolve()
        if not any(source.is_relative_to(root) for root in FIRST_PARTY_ROOTS):
            continue
        if any(source.is_relative_to(root) for root in EXCLUDED_ROOTS):
            continue
        units.add(source)
    return tuple(sorted(units))


def inspect(unit: Path, build_dir: Path, executable: str) -> tuple[Path, int, str]:
    result = subprocess.run(
        [executable, "-p", str(build_dir), "--quiet", str(unit)],
        cwd=ROOT,
        text=True,
        capture_output=True,
        check=False,
    )
    return unit, result.returncode, result.stdout + result.stderr


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-dir", type=Path, required=True)
    parser.add_argument("--jobs", type=int, default=2)
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    build_dir = args.build_dir.resolve()
    database = build_dir / "compile_commands.json"
    executable = shutil.which("clang-tidy")
    if executable is None:
        print("clang_tidy: clang-tidy is required", file=sys.stderr)
        return 2
    if not database.is_file():
        print(f"clang_tidy: compile database is missing: {database}", file=sys.stderr)
        return 2
    units = translation_units(database)
    if not units:
        print(f"clang_tidy: compile database has no first-party units: {database}")
        return 2
    with ThreadPoolExecutor(max_workers=max(1, args.jobs)) as executor:
        results = list(executor.map(lambda unit: inspect(unit, build_dir, executable), units))
    failures = [(unit, output) for unit, code, output in results if code]
    if failures:
        print(f"clang_tidy: FAILED ({len(failures)} of {len(units)} translation units)")
        for unit, output in failures:
            print(f"\n[{unit.relative_to(ROOT)}]\n{output.rstrip()}")
        return 1
    print(f"clang_tidy: passed ({len(units)} first-party translation units)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
