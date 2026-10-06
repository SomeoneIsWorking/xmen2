#!/usr/bin/env python3
"""Fail by file when tracked first-party C/C++ source drifts from .clang-format.

Generated code under src/gen is excluded; vendored code is not tracked under
src/ or tests/. `--selftest` proves the check can fail: a badly formatted
snippet must be reported and its formatted form must not.
"""

from __future__ import annotations

import argparse
from pathlib import Path
import subprocess
import sys

from llvm_tools import find_llvm_tool

ROOT = Path(__file__).resolve().parents[1]
FIRST_PARTY = ("src", "tests")
EXCLUDED = ("src/gen/",)
SUFFIXES = {".c", ".h", ".cpp", ".hpp", ".cc", ".mm", ".m"}


def tracked_sources() -> list[str]:
    result = subprocess.run(["git", "ls-files", "--", *FIRST_PARTY], cwd=ROOT,
                            text=True, capture_output=True, check=True)
    return [path for path in result.stdout.splitlines()
            if Path(path).suffix in SUFFIXES
            and not path.startswith(EXCLUDED) and (ROOT / path).is_file()]


def unformatted(executable: str, sources: list[str]) -> list[str]:
    result = subprocess.run([executable, "--dry-run", *sources], cwd=ROOT,
                            text=True, capture_output=True, check=False)
    if result.returncode and "warning:" not in result.stderr:
        raise RuntimeError(result.stderr.strip() or "clang-format failed")
    return sorted({line.split(":", 1)[0] for line in result.stderr.splitlines()
                   if "-Wclang-format-violations" in line})


def snippet_is_unformatted(executable: str, text: str) -> bool:
    result = subprocess.run(
        [executable, "--dry-run", "--Werror", "--assume-filename=snippet.c"],
        cwd=ROOT, input=text, text=True, capture_output=True, check=False)
    return result.returncode != 0


def selftest(executable: str) -> int:
    bad = "int f(int x){if(x)return 1;return 0;}\n"
    good = "int f(int x) {\n  if (x) {\n    return 1;\n  }\n  return 0;\n}\n"
    if not snippet_is_unformatted(executable, bad):
        print("check_format --selftest: FAILED -- a packed one-line function "
              "was accepted")
        return 1
    if snippet_is_unformatted(executable, good):
        print("check_format --selftest: FAILED -- a formatted function was "
              "reported")
        return 1
    print("check_format --selftest: passed (the bad snippet is reported, "
          "the good one is not)")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--selftest", action="store_true")
    args = parser.parse_args()
    executable = find_llvm_tool("clang-format")
    if executable is None:
        print("check_format: clang-format is required", file=sys.stderr)
        return 2
    if args.selftest:
        return selftest(executable)
    sources = tracked_sources()
    if not sources:
        print("check_format: no tracked first-party C/C++ sources were found",
              file=sys.stderr)
        return 2
    drifted = unformatted(executable, sources)
    if drifted:
        print(f"check_format: FAILED ({len(drifted)} of {len(sources)} files "
              "differ from .clang-format)")
        for path in drifted:
            print(f"  {path}")
        return 1
    print(f"check_format: passed ({len(sources)} first-party C/C++ files)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
