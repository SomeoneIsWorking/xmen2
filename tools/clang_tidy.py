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


def find_clang_tidy() -> str | None:
    """clang-tidy as installed, including kegs that are not on PATH.

    Homebrew does not link its llvm formula into /opt/homebrew/bin, because
    doing so would shadow Apple's toolchain. Only looking at PATH therefore
    reports "clang-tidy is required" on a machine that has it installed, and
    the check silently stops running on every such developer's machine.
    """
    found = shutil.which("clang-tidy")
    if found:
        return found
    for keg in ("/opt/homebrew/opt/llvm/bin", "/usr/local/opt/llvm/bin"):
        candidate = Path(keg) / "clang-tidy"
        if candidate.is_file():
            return str(candidate)
    return None


def sysroot_arguments() -> list[str]:
    """Where the C++ standard headers live, for a non-Apple clang-tidy.

    A Homebrew clang-tidy has no built-in idea of the macOS SDK, so every unit
    that includes <string> fails to find it and the whole check reports errors
    that have nothing to do with the code. Apple's own clang-tidy needs none of
    this, and passing it costs nothing.
    """
    if sys.platform != "darwin":
        return []
    result = subprocess.run(
        ["xcrun", "--show-sdk-path"], text=True, capture_output=True, check=False)
    sdk = result.stdout.strip()
    if result.returncode or not sdk:
        return []
    return ["--extra-arg=-isysroot", f"--extra-arg={sdk}"]


def inspect(unit: Path, build_dir: Path, executable: str,
            extra: list[str]) -> tuple[Path, int, str]:
    result = subprocess.run(
        [executable, "-p", str(build_dir), "--quiet", *extra, str(unit)],
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
    executable = find_clang_tidy()
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
        extra = sysroot_arguments()
        results = list(
            executor.map(lambda unit: inspect(unit, build_dir, executable, extra),
                         units))
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
