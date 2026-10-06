"""Mechanical ownership and retired-methodology policy for X-Men 2."""

from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path
import re
from typing import Mapping


RETIRED_STATIC_PATHS = (
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
    "tools/build_shim.sh",
    "tools/gen_probes.py",
    "tools/proxy_d3d8/probe_hook.c",
    "src/recomp/gen/probe_table.h",
    "src/recomp/gen/probe_stubs.S",
)

IGNORED_TREES = {".git", ".venv", "build", "scratch", "vendor", "game"}
TEXT_SUFFIXES = {
    ".c",
    ".cc",
    ".cmake",
    ".cpp",
    ".h",
    ".hpp",
    ".ini",
    ".json",
    ".md",
    ".py",
    ".sh",
    ".toml",
    ".txt",
    ".yaml",
    ".yml",
}
TEXT_NAMES = {"CMakeLists.txt", "Dockerfile", "LICENSE", "Makefile"}
STATIC_POLICY_OWNERS = {
    "AGENTS.md",
    "docs/project-goals.md",
    "tools/runtime_boundary.py",
    "tools/verify_source_policy.py",
    "tools/x2_source_policy.py",
}

CONFIG_OWNER = "src/config/environment.c"
PRODUCT_SOURCE_GROUPS = ("x2_runtime_services", "x2_rmlui_ui")
DIRECT_ENVIRONMENT = re.compile(
    r"\b(?:getenv|secure_getenv|_wgetenv|setenv|unsetenv)\s*\(|"
    r"\bextern\s+char\s*\*\*\s*environ\b"
)
UNTYPED_ENVIRONMENT = re.compile(r"\bx2_environment_get\s*\(")
DIRECT_DIAGNOSTICS = (
    re.compile(r"(?<![A-Za-z0-9_])(?:printf|puts|putchar)\s*\("),
    re.compile(r"\b(?:f|vf)printf\s*\(\s*(?:stderr|stdout)\b"),
    re.compile(
        r"\b(?:fputs|fputc)\s*\([^;]*\b(?:stderr|stdout)\b", re.DOTALL
    ),
    re.compile(r"\bstd::c(?:out|err|log)\b"),
    re.compile(r"\bOutputDebugString(?:A|W)?\s*\("),
)

STATIC_TOOL_PATTERNS = (
    re.compile(r"\b(?:static[- ]?)?recomp(?:il(?:e[rd]?|ation))?\b", re.IGNORECASE),
    re.compile(r"tools/gen_probes\.py"),
    re.compile(r"probe_(?:table\.h|stubs\.S)"),
    re.compile(r"src/recomp"),
    re.compile(r"recomp-x86"),
)


@dataclass(frozen=True)
class Violation:
    location: str
    reason: str


def _cmake_sources(cmake_text: str, call_start: str, description: str) -> tuple[str, ...]:
    match = re.search(call_start, cmake_text)
    if match is None:
        raise ValueError(f"CMakeLists.txt has no {description}")
    depth = 1
    end = match.end()
    while end < len(cmake_text) and depth:
        if cmake_text[end] == "(":
            depth += 1
        elif cmake_text[end] == ")":
            depth -= 1
        end += 1
    if depth:
        raise ValueError(f"CMakeLists.txt has an unterminated {description}")
    sources = tuple(re.findall(r"(?<!\S)(src/[^\s\)]+)", cmake_text[match.end() : end - 1]))
    if not sources:
        raise ValueError(f"{description} contains zero first-party source files")
    return sources


def shipping_sources(cmake_text: str) -> tuple[str, ...]:
    sources = list(
        _cmake_sources(
            cmake_text,
            r"\bset\s*\(\s*X2_NATIVE_SOURCES\b",
            "X2_NATIVE_SOURCES set",
        )
    )
    for target in PRODUCT_SOURCE_GROUPS:
        sources.extend(
            _cmake_sources(
                cmake_text,
                rf"\badd_library\s*\(\s*{re.escape(target)}\b",
                f"{target} library source group",
            )
        )
    return tuple(dict.fromkeys(sources))


def text_violations(
    source_text: Mapping[str, str], all_text: Mapping[str, str] | None = None
) -> list[Violation]:
    violations: list[Violation] = []
    for relative, text in source_text.items():
        if relative != CONFIG_OWNER and DIRECT_ENVIRONMENT.search(text):
            violations.append(
                Violation(relative, f"direct environment read bypasses {CONFIG_OWNER}")
            )
        if UNTYPED_ENVIRONMENT.search(text):
            violations.append(
                Violation(relative, "unrestricted configuration lookup remains")
            )
        if any(pattern.search(text) for pattern in DIRECT_DIAGNOSTICS):
            violations.append(Violation(relative, "direct diagnostic output bypasses Lucent"))
    repository_text = source_text if all_text is None else all_text
    for relative, text in repository_text.items():
        if relative in STATIC_POLICY_OWNERS:
            continue
        if any(
            pattern.search(relative) or pattern.search(text) for pattern in STATIC_TOOL_PATTERNS
        ):
            violations.append(Violation(relative, "retired static-recompiler record remains"))
    return violations


def first_party_text(root: Path) -> dict[str, str]:
    texts: dict[str, str] = {}
    for path in root.rglob("*"):
        relative_path = path.relative_to(root)
        if any(part in IGNORED_TREES for part in relative_path.parts):
            continue
        if not path.is_file() or (
            path.suffix.lower() not in TEXT_SUFFIXES
            and path.name not in TEXT_NAMES
            and not path.name.startswith(".")
        ):
            continue
        relative = relative_path.as_posix()
        try:
            texts[relative] = path.read_text(encoding="utf-8")
        except UnicodeDecodeError as exc:
            raise ValueError(f"prospective policy input is not UTF-8 text: {relative}") from exc
    return texts


def retired_path_violations(root: Path) -> list[Violation]:
    return [
        Violation(path, "retired static-product/tool path exists")
        for path in RETIRED_STATIC_PATHS
        if (root / path).exists() or (root / path).is_symlink()
    ]


def source_violations(root: Path) -> tuple[int, list[Violation]]:
    violations = retired_path_violations(root)
    try:
        product_sources = shipping_sources((root / "CMakeLists.txt").read_text(encoding="utf-8"))
    except (OSError, ValueError) as exc:
        violations.append(Violation("CMakeLists.txt", str(exc)))
        return len(RETIRED_STATIC_PATHS), violations
    policy_paths = (*product_sources, "tools/build_stocklog.py", "tools/build_shim.py")
    for relative in policy_paths:
        path = root / relative
        if not path.is_file():
            violations.append(Violation(relative, "declared policy input is missing"))
            continue
    repository_text = first_party_text(root)
    for relative in repository_text:
        if relative.endswith(".sh") and relative != "run.sh":
            violations.append(Violation(relative, "non-launcher shell automation remains"))
    product_boundary_text = {
        relative: text
        for relative, text in repository_text.items()
        if relative == CONFIG_OWNER or relative.startswith("src/")
    }
    violations.extend(text_violations(product_boundary_text, repository_text))
    return len(RETIRED_STATIC_PATHS) + len(repository_text), violations
