#!/usr/bin/env python3
"""Enforce source ownership by refusing new or growing monoliths in host
source and Python tooling."""

from __future__ import annotations

import sys
from pathlib import Path


# The user set the cap for new host files at 1,200 lines (2026-09-25), and for
# tools/ Python too (2026-10-06); no tools file was over it then. The
# legacy entries below stay frozen at their own measured sizes, including
# those under the cap: a known monolith still may not grow.
DEFAULT_LIMIT = 1200

# Existing monoliths are frozen at their measured size. They are debt, not
# examples: extraction should lower these numbers, never raise them.
#
# Re-baselined 2026-09-03 when the tree adopted clang-format's LLVM style. Not a
# relaxation: the code is byte-for-byte the same work, and 2-space indent with
# an 80-column limit renders it in about 10% more lines. The 2026-08-20 numbers
# measured a different rendering of the same files, so comparing against them
# would have failed 26 files that nobody touched. Each entry records what it
# was, so the ratchet is still auditable.
LEGACY_LIMITS = {
    "src/native/kernel32.cpp": 3087,            # was 3624; virtual memory -> kernel32_virtual.cpp
    "src/native/x86rt_native.cpp": 1856,        # was 1865; dladdr -> host_code_location.cpp
    "src/native/x2native.cpp": 2027,            # was 2329, 2152; signals -> fault_signals_*.cpp
    "src/d3d8/d3d8_drawcall.cpp": 1650,         # was 1695; VS source -> d3d8_vs_draw.cpp
    "src/d3d8/d3d8_device.cpp": 1633,           # bindings -> d3d8_device_bindings.cpp
    "src/native/crt.cpp": 1349,                 # was 1353, then 1535; stdio -> crt_stdio.cpp
    "src/gpu/gpu_draw.cpp": 1016,               # was 1123; uniforms -> gpu_vertex_uniforms.cpp
    "src/d3d8/d3d8_report.cpp": 1433,           # was 1514; gamma -> d3d8_gamma_selftest.cpp
    "src/native/threads.cpp": 673,              # was 1070, 842, 717, 694; reports, quantum, stack/TIB -> threads_memory.cpp
    "src/gpu/gpu_device.cpp": 753,              # was 758; pass attachments -> gpu_pass_attachments.cpp
    "src/d3d8/d3d8_resource.cpp": 1050,         # was 924
    "src/native/dinput_pad.cpp": 378,           # sampler split into dinput_pad_sample.cpp
    "src/native/win32_sdl.cpp": 1025,           # was 930
    "src/native/conversation.cpp": 968,         # was 958
    "src/native/dsound.cpp": 745,               # was 1097 before the mixer split
    "src/gpu/gpu_selftest.cpp": 223,            # was 354; lit/mvp -> gpu_lit_mvp_selftest.cpp
    "src/native/input_probe.cpp": 597,          # was 606
    "src/native/dinput_device.cpp": 611,        # was 524
    "src/native/advapi32.cpp": 710,             # was 599
    "src/d3d8/d3d8_com.cpp": 620,               # was 609
    "src/native/dinput8.cpp": 533,              # was 516
    "src/native/heartbeat.cpp": 429,            # JIT snapshot policy stays in x86_engine.cpp
}


SOURCE_SUFFIXES = {".c", ".h", ".cpp", ".hpp"}
TOOL_SUFFIXES = {".py"}


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
    for path in (root / "tools").rglob("*"):
        if path.is_file() and path.suffix in TOOL_SUFFIXES:
            relative = path.relative_to(root).as_posix()
            counts[relative] = len(path.read_text(encoding="utf-8").splitlines())
    return counts


def selftest() -> int:
    # Read the legacy limit rather than repeating it: a ratchet then moves
    # both the rule and its selftest, instead of failing the selftest.
    legacy = LEGACY_LIMITS["src/native/kernel32.cpp"]
    good = {"src/new.c": DEFAULT_LIMIT, "src/native/kernel32.cpp": legacy,
            "tools/new.py": DEFAULT_LIMIT}
    bad = {"src/new.c": DEFAULT_LIMIT + 1, "src/native/kernel32.cpp": legacy + 1,
           "tools/new.py": DEFAULT_LIMIT + 1}
    if violations(good):
        print("check_structure selftest: valid source was rejected", file=sys.stderr)
        return 1
    found = violations(bad)
    if len(found) != 3 or not any(
        f"{DEFAULT_LIMIT + 1} lines (limit {DEFAULT_LIMIT})" in failure for failure in found
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
    print(f"check_structure: all host source and tools Python respect the "
          f"{DEFAULT_LIMIT}-line cap")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
