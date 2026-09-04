#!/usr/bin/env python3
"""Prove that gameplay defaults to JIT and exposes no interpreter selector."""

from __future__ import annotations

import argparse
from dataclasses import dataclass
from pathlib import Path
import re
import subprocess
import tempfile
from typing import Mapping


RETIRED_PATHS = (
    "src/recomp",
    "src/x86fault.c",
    "src/x86watch.c",
    "src/x86watch_memory.c",
    "src/x86watch_memory.h",
    "src/x86watch_stack.c",
    "src/x86watch_stack.h",
    "src/x86watch_trace.c",
    "src/x86watch_trace.h",
    "tests/test_x86watch_memory.c",
    "tests/test_x86watch_stack.c",
    "tests/test_x86watch_trace.c",
    "docs/issues/0069-hosted-boundary-watch-can-strand-crimovie-while.md",
    "vendor/xboxrecomp",
    "xbox",
    "tools/add_module.sh",
    "tools/build_shim.sh",
    "tools/run_shim.sh",
    "tools/gen_probes.py",
    "tools/proxy_d3d8/probe_hook.c",
    "tools/native_discover.py",
    "tools/reinject_bytes.py",
    "tools/seed_code_imms.py",
    "tools/seed_data_ptrs.py",
    "tools/seed_import_thunks.py",
    "tools/seed_relocs.py",
    "tools/xbox_discover.py",
    "tools/xbe_query.py",
    "tools/xbox_relift.py",
    "tools/xbox_run.py",
    "tools/xbox_vtable_seeds.py",
    "tests/difftest.c",
    "tests/findrepro.c",
    "src/native/x86_engine_bridge.c",
    "src/native/x86_engine_frames.c",
    "src/native/x86_engine_frames.h",
    "src/native/x86_engine_internal.h",
    "src/native/x86_tail_policy.c",
    "src/native/x86_tail_policy.h",
    "src/native/x86_reached.c",
    "src/native/x86_reached.h",
    "src/native/engine_leaf_thunks.c",
    "src/native/engine_leaf_thunks.h",
    "tests/test_flags.c",
    "tests/test_mmx.c",
    "tests/test_rotate.c",
    "tests/test_sse.c",
    "tests/test_x86_tail_policy.c",
)

SOURCE_RULES = {
    "CMakeLists.txt": (
        (
            r"target_link_libraries\(x2native\s+PRIVATE\s+x86port\)",
            "x2native links the compatibility target instead of x86port_runtime",
        ),
        (
            r"X2_NATIVE_O0|tools/recomp\.py|tools/xbox_(?:relift|discover|run)\.py",
            "retired static-product build or tool wiring remains",
        ),
    ),
    "src/config/runtime_cvars.cpp": (
        (r"g_engine\s*\{|jit\.verify", "an execution selector or oracle knob remains"),
    ),
    "src/config/runtime_cvars.h": (
        (r"engine\s+string|jit\.verify", "an execution selector or oracle knob remains"),
    ),
    "src/native/x86_engine.c": (
        (
            r"x86p_engine_resolve|kX86pEngineInterpreter",
            "the product can explicitly select the diagnostic interpreter",
        ),
        (r"x86p_jit_engine_set_verify", "the product can shadow-run the test oracle"),
    ),
    "src/native/x2native_options.c": (
        (r"\bengine\s*=", "the CLI exposes an engine selector"),
    ),
    "src/runtime/x86_abi/x86rt.h": (
        (
            r"typedef\s+struct\s+CPU|\bflag_op\b|\bflag_a\b|\bflag_r\b|\bmmx\s*\[",
            "a retired title-owned architectural CPU model remains",
        ),
    ),
}

REQUIRED_SOURCE_PATTERNS = {
    "CMakeLists.txt": (
        (
            r"target_link_libraries\(x2native\s+PRIVATE\s+x86port_runtime\)",
            "x2native does not explicitly link x86port_runtime",
        ),
        (
            r"if\(NOT TARGET x86port_runtime\)",
            "CMake does not refuse a missing product-only runtime target",
        ),
    ),
    "src/runtime/x86_abi/x86rt.h": (
        (
            r"typedef\s+X86pCpu\s+CPU\s*;",
            "the title ABI does not alias the canonical x86port CPU type",
        ),
    ),
}

BANNED_SYMBOLS = (
    re.compile(r"(?:^|\s)x86p_engine_resolve(?:$|\s)"),
    re.compile(r"(?:^|\s)x86p_jit_engine_set_verify(?:$|\s)"),
)


@dataclass(frozen=True)
class Violation:
    location: str
    reason: str


def source_violations(root: Path) -> list[Violation]:
    violations = retired_path_violations(root)
    source_text = {
        relative: (root / relative).read_text(encoding="utf-8")
        for relative in set(SOURCE_RULES) | set(REQUIRED_SOURCE_PATTERNS)
    }
    violations.extend(source_text_violations(source_text))
    return violations


def retired_path_violations(root: Path) -> list[Violation]:
    return [
        Violation(path, "retired static-product path still exists")
        for path in RETIRED_PATHS
        if (root / path).exists() or (root / path).is_symlink()
    ]


def source_text_violations(source_text: Mapping[str, str]) -> list[Violation]:
    """Apply content rules to injected text so the discriminator needs no files."""
    violations: list[Violation] = []
    for relative, rules in SOURCE_RULES.items():
        text = source_text[relative]
        violations.extend(
            Violation(relative, reason)
            for pattern, reason in rules
            if re.search(pattern, text, re.MULTILINE)
        )
    for relative, rules in REQUIRED_SOURCE_PATTERNS.items():
        text = source_text[relative]
        violations.extend(
            Violation(relative, reason)
            for pattern, reason in rules
            if not re.search(pattern, text, re.MULTILINE)
        )
    return violations


def banned_symbol_lines(nm_output: str) -> list[str]:
    lines = [line for line in nm_output.splitlines() if line.strip()]
    return [line for line in lines if any(rule.search(line) for rule in BANNED_SYMBOLS)]


def binary_violations(binary: Path) -> list[Violation]:
    if not binary.is_file():
        return [Violation(str(binary), "gameplay binary does not exist")]
    result = subprocess.run(["nm", "-a", str(binary)], text=True, capture_output=True, check=False)
    if result.returncode:
        detail = result.stderr.strip() or "nm produced no diagnostic"
        return [Violation(str(binary), f"could not inspect link closure: {detail}")]
    lines = [line for line in result.stdout.splitlines() if line.strip()]
    if not lines:
        return [Violation(str(binary), "nm reported zero symbols; link closure is unproven")]
    return [
        Violation(str(binary), f"test-oracle symbol linked into gameplay: {line.strip()}")
        for line in banned_symbol_lines(result.stdout)
    ]


def print_result(label: str, checked: int, violations: list[Violation]) -> int:
    if violations:
        print(f"runtime_boundary: {label} FAILED ({checked} checks)")
        for violation in violations:
            print(f"  {violation.location}: {violation.reason}")
        return 1
    print(f"runtime_boundary: {label} passed ({checked} checks, 0 violations)")
    return 0


def selftest() -> int:
    source_text = {relative: "" for relative in SOURCE_RULES}
    source_text["CMakeLists.txt"] = (
        "if(NOT TARGET x86port_runtime)\nendif()\n"
        "target_link_libraries(x2native PRIVATE x86port_runtime)\n"
    )
    source_text["src/runtime/x86_abi/x86rt.h"] = "typedef X86pCpu CPU;\n"
    if source_text_violations(source_text):
        print("runtime_boundary selftest: clean source fixture was rejected")
        return 1
    source_text["src/native/x2native_options.c"] = 'parse("engine=retired");\n'
    violations = source_text_violations(source_text)
    if not any("selector" in violation.reason for violation in violations):
        print("runtime_boundary selftest: selector leak was not detected")
        return 1
    clean_symbols = "00000000 T x86p_jit_engine_run\n"
    leaked_symbols = clean_symbols + "00000010 T x86p_engine_resolve\n"
    if banned_symbol_lines(clean_symbols) or not banned_symbol_lines(leaked_symbols):
        print("runtime_boundary selftest: binary-symbol discriminator failed")
        return 1
    with tempfile.TemporaryDirectory() as directory:
        root = Path(directory)
        retired_path = root / "src/x86watch.c"
        retired_path.parent.mkdir(parents=True)
        retired_path.touch()
        retired = retired_path_violations(root)
    if not any(violation.location == "src/x86watch.c" for violation in retired):
        print("runtime_boundary selftest: retired x86watch path escaped")
        return 1
    print(
        "runtime_boundary selftest: accepts JIT-default input and rejects selector and "
        "retired-path leaks"
    )
    return 0


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    action = parser.add_mutually_exclusive_group(required=True)
    action.add_argument("--source", action="store_true")
    action.add_argument("--binary", type=Path)
    action.add_argument("--selftest", action="store_true")
    parser.add_argument("--root", type=Path, default=Path(__file__).resolve().parents[1])
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    if args.selftest:
        return selftest()
    if args.source:
        checks = len(RETIRED_PATHS) + sum(len(rules) for rules in SOURCE_RULES.values())
        checks += sum(len(rules) for rules in REQUIRED_SOURCE_PATTERNS.values())
        return print_result("source boundary", checks, source_violations(args.root))
    violations = binary_violations(args.binary)
    return print_result("binary link closure", len(BANNED_SYMBOLS), violations)


if __name__ == "__main__":
    raise SystemExit(main())
