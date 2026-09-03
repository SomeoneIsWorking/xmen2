#!/usr/bin/env python3
"""Enforce source ownership by refusing new or growing host monoliths."""

from __future__ import annotations

import sys
from pathlib import Path


DEFAULT_LIMIT = 500

# Existing monoliths are frozen at the measured size from 2026-08-20. They are
# debt, not examples: extraction should lower these numbers, never raise them.
LEGACY_LIMITS = {
    "src/native/kernel32.c": 4010,
    "src/native/x86rt_native.c": 2593,
    "src/native/x2native.c": 2278,
    "src/d3d8/d3d8_drawcall.c": 2273,
    "src/d3d8/d3d8_device.c": 1934,
    "src/native/crt.c": 1390,
    "src/recomp/x86rt.h": 1499,
    "src/gpu/gpu_draw.c": 1538,
    "src/d3d8/d3d8_report.c": 1408,
    "src/native/threads.c": 1175,
    "src/gpu/gpu_device.c": 976,
    "src/d3d8/d3d8_resource.c": 1004,
    "src/native/dinput_pad.c": 1003,
    "src/native/win32_sdl.c": 984,
    "src/native/conversation.c": 979,
    "src/native/dsound.c": 766,
    "src/gpu/gpu_selftest.c": 646,
    "src/native/input_probe.c": 619,
    "src/native/dinput_device.c": 524,
    "src/native/advapi32.c": 610,
    "src/d3d8/d3d8_com.c": 609,
    "src/native/dinput8.c": 594,
    "src/native/heartbeat.c": 509,
}

SOURCE_SUFFIXES = {".c", ".h", ".cpp", ".hpp"}


def violations(counts: dict[str, int]) -> list[str]:
    failures = []
    for path, lines in sorted(counts.items()):
        limit = LEGACY_LIMITS.get(path, DEFAULT_LIMIT)
        if lines > limit:
            failures.append(f"{path}: {lines} lines (limit {limit})")
    return failures


def source_counts(root: Path) -> dict[str, int]:
    counts = {}
    for path in (root / "src").rglob("*"):
        relative = path.relative_to(root).as_posix()
        if not path.is_file() or path.suffix not in SOURCE_SUFFIXES:
            continue
        # src/gen holds published assets (glyph atlas, font ratios): bytes with
        # a .h wrapper, not code anyone owns or extracts.
        if relative.startswith("src/gen/"):
            continue
        counts[relative] = len(path.read_text(encoding="utf-8").splitlines())
    return counts


def selftest() -> int:
    good = {"src/new.c": DEFAULT_LIMIT, "src/native/kernel32.c": 4010}
    bad = {"src/new.c": DEFAULT_LIMIT + 1, "src/native/kernel32.c": 4011}
    if violations(good):
        print("check_structure selftest: valid source was rejected", file=sys.stderr)
        return 1
    found = violations(bad)
    if len(found) != 2 or not any(
        "501 lines (limit 500)" in failure for failure in found
    ):
        print("check_structure selftest: growth was not detected", file=sys.stderr)
        return 1
    print("check_structure selftest: accepts the boundary and rejects growth")
    return 0


def main() -> int:
    if sys.argv[1:] == ["--selftest"]:
        return selftest()
    if sys.argv[1:]:
        print("usage: check_structure.py [--selftest]", file=sys.stderr)
        return 2
    root = Path(__file__).resolve().parents[1]
    failures = violations(source_counts(root))
    if failures:
        print("Source structure limits exceeded:", file=sys.stderr)
        for failure in failures:
            print(f"  {failure}", file=sys.stderr)
        print("Extract a cohesive owner; do not raise the limit.", file=sys.stderr)
        return 1
    print(f"check_structure: all host source files respect the {DEFAULT_LIMIT}-line cap")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
