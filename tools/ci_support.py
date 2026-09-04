"""Asset-free CI policy and host-support ownership.

The CI entry point, workflow, and documentation all consume this table.  A
platform that cannot build or run the gameplay product is represented as such;
CI must not manufacture a game install or a generated calibration header to
turn that absence into a green product claim.
"""

from __future__ import annotations

from dataclasses import dataclass
import platform
from pathlib import Path
import re
import subprocess
import sys
from typing import Mapping, Sequence


@dataclass(frozen=True)
class TargetSupport:
    key: str
    system: str
    machine: str
    verification: str
    gameplay_jit: bool
    native_components: bool
    explanation: str


TARGETS: Mapping[str, TargetSupport] = {
    "linux-x86_64": TargetSupport(
        key="linux-x86_64",
        system="Linux",
        machine="x86_64",
        verification="policy + native/JIT component build and tests",
        gameplay_jit=True,
        native_components=True,
        explanation=(
            "x86port provides the shipping x86-64 JIT; CI remains asset-free "
            "and therefore does not claim a gameplay run"
        ),
    ),
    "macos-arm64": TargetSupport(
        key="macos-arm64",
        system="Darwin",
        machine="arm64",
        verification="policy + platform-neutral native component build and tests",
        gameplay_jit=False,
        native_components=True,
        explanation="the ARM64 x86port JIT backend does not exist yet",
    ),
    "windows-x86_64": TargetSupport(
        key="windows-x86_64",
        system="Windows",
        machine="x86_64",
        verification="policy only",
        gameplay_jit=False,
        native_components=False,
        explanation="the native Windows host is not implemented",
    ),
    "android-arm64": TargetSupport(
        key="android-arm64",
        system="Linux",
        machine="x86_64",
        verification="policy only",
        gameplay_jit=False,
        native_components=False,
        explanation=(
            "the ARM64 x86port JIT backend is absent and the current native "
            "target needs a player-derived font calibration header"
        ),
    ),
}


class CiFailure(RuntimeError):
    """A named CI refusal or failed subprocess, suitable for one-line output."""


POLICY_TESTS = (
    "tests/test_ci.py",
    "tests/test_process_status.py",
    "tests/test_android_qualify.py",
    "tests/test_android_release.py",
    "tests/test_port_tool_migrations.py",
    "tests/test_selector_probe.py",
    "tests/test_x2ctl.py",
)

COMMON_NATIVE_TARGETS = (
    "test_x2_log",
    "test_config_directory",
    "test_touch_controls",
    "test_gameplay_control",
    "test_touch_layout",
    "test_gpu_frame_timing",
    "test_alchemy_controller_adapter",
)

LINUX_JIT_TARGETS = (
    "test_x86_guest_call_stack",
    "test_jit_intercept",
    "test_x86_import_fastpath",
)

COMMON_NATIVE_TESTS = (
    "x2_log",
    "config_directory",
    "touch_controls",
    "gameplay_control",
    "touch_layout",
    "gpu_frame_timing",
    "alchemy_controller_adapter",
)

LINUX_JIT_TESTS = (
    "x86_guest_call_stack",
    "jit_intercept",
    "x86_import_fastpath",
)

ACTION_REFERENCE = re.compile(r"\buses:\s*([^\s@]+)@([^\s#]+)")
ACTION_STEP = re.compile(
    r"^(?P<indent>[ \t]*)-\s+uses:\s*(?P<action>[^\s@]+)@(?P<reference>[^\s#]+)",
    re.MULTILINE,
)
FORBIDDEN_WORKFLOW_INPUT = re.compile(
    r"\b(?:GAME_PC_DIR|XBOX_ISO|WINE_PREFIX)\b|\bsecrets\.", re.IGNORECASE
)
PINNED_ACTIONS: Mapping[str, str] = {
    "actions/checkout": "3d3c42e5aac5ba805825da76410c181273ba90b1",
    "actions/setup-python": "5fda3b95a4ea91299a34e894583c3862153e4b97",
    "astral-sh/setup-uv": "20cfd1bf945f4377ade1205e4dbc17946fc9a30d",
}
REQUIRED_TOOLCHAIN_SNIPPETS = (
    'UV_VERSION: "0.10.12"',
    'PYTHON_VERSION: "3.13"',
    "runs-on: ubuntu-26.04",
    "runs-on: macos-15",
    "runs-on: windows-2025",
    "clang-20",
    "/Applications/Xcode_16.4.app/Contents/Developer",
)


def action_step_bodies(text: str, action_name: str) -> tuple[str, ...]:
    """Return the indented YAML body owned by every matching action step."""
    lines = text.splitlines()
    bodies: list[str] = []
    for index, line in enumerate(lines):
        match = ACTION_STEP.match(line)
        if match is None or match.group("action") != action_name:
            continue
        indentation = len(match.group("indent"))
        body: list[str] = []
        for following in lines[index + 1 :]:
            if following.strip() and len(following) - len(following.lstrip()) <= indentation:
                break
            body.append(following)
        bodies.append("\n".join(body))
    return tuple(bodies)


def checkout_policy_violations(text: str) -> list[str]:
    failures: list[str] = []
    checkout_references = [
        reference for action, reference in ACTION_REFERENCE.findall(text)
        if action == "actions/checkout"
    ]
    bodies = action_step_bodies(text, "actions/checkout")
    if len(bodies) != len(checkout_references):
        failures.append(
            "could not parse every actions/checkout step for checkout policy"
        )
    for index, body in enumerate(bodies, 1):
        fields: dict[str, list[str]] = {"fetch-depth": [], "persist-credentials": []}
        for name in fields:
            fields[name] = re.findall(
                rf"(?m)^\s+{re.escape(name)}:\s*([^\s#]+)", body
            )
        if fields["fetch-depth"] != ["0"]:
            failures.append(
                f"actions/checkout step {index} must fetch full history with fetch-depth: 0"
            )
        if fields["persist-credentials"] != ["false"]:
            failures.append(
                f"actions/checkout step {index} must set persist-credentials: false"
            )
    return failures


def normalized_machine(value: str) -> str:
    lowered = value.casefold()
    if lowered in {"amd64", "x64", "x86_64"}:
        return "x86_64"
    if lowered in {"aarch64", "arm64"}:
        return "arm64"
    return lowered


def require_runner(target: TargetSupport) -> None:
    actual_system = platform.system()
    actual_machine = normalized_machine(platform.machine())
    if actual_system != target.system or actual_machine != target.machine:
        raise CiFailure(
            f"{target.key} requires {target.system}/{target.machine}; runner is "
            f"{actual_system}/{actual_machine}"
        )


def run_checked(command: Sequence[str], root: Path, environment: Mapping[str, str]) -> None:
    print("+", " ".join(command), flush=True)
    result = subprocess.run(command, cwd=root, env=dict(environment), check=False)
    if result.returncode:
        raise CiFailure(
            f"command failed with exit {result.returncode}: {' '.join(command)}"
        )


def policy_commands(root: Path) -> tuple[tuple[str, ...], ...]:
    python = sys.executable
    return (
        (python, str(root / "tools/verify_source_policy.py")),
        (python, str(root / "tools/verify_source_policy.py"), "--selftest"),
        (python, str(root / "tools/runtime_boundary.py"), "--source"),
        (python, str(root / "tools/check_structure.py")),
        (python, str(root / "tools/check_structure.py"), "--selftest"),
        (python, "-m", "ruff", "check", "tools", "tests"),
        (python, "-m", "pytest", "-q", *POLICY_TESTS),
    )


def workflow_violations(text: str) -> list[str]:
    failures: list[str] = []
    references = ACTION_REFERENCE.findall(text)
    if not references:
        failures.append("workflow uses no pinned actions")
    for action, reference in references:
        if re.fullmatch(r"[0-9a-f]{40}", reference) is None:
            failures.append(f"action reference is not an exact commit: {action}@{reference}")
            continue
        expected = PINNED_ACTIONS.get(action)
        if expected is None:
            failures.append(f"workflow uses an unowned action: {action}")
        elif reference != expected:
            failures.append(
                f"{action} is pinned to {reference}, expected reviewed commit {expected}"
            )
    failures.extend(checkout_policy_violations(text))
    if "-latest" in text:
        failures.append("workflow uses a moving latest runner or toolchain")
    for snippet in REQUIRED_TOOLCHAIN_SNIPPETS:
        if snippet not in text:
            failures.append(f"workflow omits pinned toolchain declaration: {snippet}")
    if FORBIDDEN_WORKFLOW_INPUT.search(text):
        failures.append("workflow references game/oracle inputs or GitHub secrets")
    for target in TARGETS:
        invocation = f"tools/ci.py policy --target {target}"
        if invocation not in text:
            failures.append(f"workflow omits {target} policy invocation")
    for target in ("linux-x86_64", "macos-arm64"):
        invocation = f"tools/ci.py native-components --target {target}"
        if invocation not in text:
            failures.append(f"workflow omits {target} native-component invocation")
    if "tools/ci.py native-components --target windows-x86_64" in text:
        failures.append("workflow pretends the unsupported Windows host builds")
    if "tools/ci.py native-components --target android-arm64" in text:
        failures.append("workflow pretends the unsupported Android product builds")
    return failures


def verify_workflow(root: Path) -> None:
    workflow = root / ".github/workflows/asset-free.yml"
    if not workflow.is_file():
        raise CiFailure(f"asset-free workflow is missing: {workflow}")
    failures = workflow_violations(workflow.read_text(encoding="utf-8"))
    if failures:
        raise CiFailure("invalid asset-free workflow:\n  " + "\n  ".join(failures))


def native_targets(target: TargetSupport) -> tuple[str, ...]:
    if not target.native_components:
        raise CiFailure(f"{target.key} is {target.verification}: {target.explanation}")
    if target.key == "linux-x86_64":
        return COMMON_NATIVE_TARGETS + LINUX_JIT_TARGETS
    return COMMON_NATIVE_TARGETS


def native_test_regex(target: TargetSupport) -> str:
    names = COMMON_NATIVE_TESTS
    if target.key == "linux-x86_64":
        names += LINUX_JIT_TESTS
    return "^(" + "|".join(names) + ")$"


def markdown_support_rows() -> tuple[str, ...]:
    labels = {
        "linux-x86_64": "Linux x86-64",
        "macos-arm64": "Apple Silicon macOS",
        "windows-x86_64": "Windows x86-64",
        "android-arm64": "Android ARM64",
    }
    rows = []
    for key, target in TARGETS.items():
        status = (
            "JIT available; CI makes no asset-backed gameplay claim"
            if target.gameplay_jit
            else f"Unsupported: {target.explanation}"
        )
        rows.append(f"| {labels[key]} | {target.verification} | {status} |")
    return tuple(rows)
