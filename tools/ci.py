#!/usr/bin/env python3
"""Run the repository's asset-free CI contracts.

This tool never provisions, discovers, or opens a game install.  Linux and
Apple Silicon compile only explicitly listed native components; policy-only
targets remain policy-only until their missing host/JIT boundary is real.
"""

from __future__ import annotations

import argparse
import os
from pathlib import Path
import sys

try:
    from tools import ci_support
except ImportError:
    import ci_support


ROOT = Path(__file__).resolve().parents[1]


def target_named(name: str) -> ci_support.TargetSupport:
    try:
        return ci_support.TARGETS[name]
    except KeyError as error:
        choices = ", ".join(ci_support.TARGETS)
        raise SystemExit(f"ci: unknown target {name!r}; expected {choices}") from error


def policy(target: ci_support.TargetSupport) -> None:
    ci_support.require_runner(target)
    ci_support.verify_workflow(ROOT)
    environment = os.environ.copy()
    environment["PYTHONDONTWRITEBYTECODE"] = "1"
    for command in ci_support.policy_commands(ROOT):
        ci_support.run_checked(command, ROOT, environment)
    print(f"ci: {target.key}: {target.verification}; {target.explanation}")


def ensure_shared(environment: dict[str, str]) -> None:
    sys.path.insert(0, str(ROOT))
    import bootstrap

    bootstrap.ensure_shared()
    environment["SHARED_DIR"] = str(ROOT / "vendor/shared")
    for repo in bootstrap.SHARED_REPOS:
        name = repo.name.replace("-", "_").upper() + "_DIR"
        value = os.environ.get(name)
        if value:
            environment[name] = value


def native_components(target: ci_support.TargetSupport) -> None:
    ci_support.require_runner(target)
    targets = ci_support.native_targets(target)
    environment = os.environ.copy()
    environment["PYTHONDONTWRITEBYTECODE"] = "1"
    environment.setdefault("CC", "clang")
    environment.setdefault("CXX", "clang++")
    ensure_shared(environment)
    build = ROOT / "build" / f"ci-{target.key}"
    configure = (
        "cmake",
        "-S",
        str(ROOT),
        "-B",
        str(build),
        "-G",
        "Ninja",
        "-DCMAKE_BUILD_TYPE=Debug",
        f"-DPython3_EXECUTABLE={sys.executable}",
    )
    ci_support.run_checked(configure, ROOT, environment)
    ci_support.run_checked(
        ("cmake", "--build", str(build), "--target", *targets, "--parallel", "2"),
        ROOT,
        environment,
    )
    ci_support.run_checked(
        (
            "ctest",
            "--test-dir",
            str(build),
            "--output-on-failure",
            "-R",
            ci_support.native_test_regex(target),
        ),
        ROOT,
        environment,
    )
    scope = "native/JIT" if target.gameplay_jit else "platform-neutral native"
    print(f"ci: {target.key}: passed {scope} component checks without game assets")


def selftest() -> None:
    clean = f"""
env:
  UV_VERSION: "0.10.12"
  PYTHON_VERSION: "3.13"
jobs:
  linux:
    runs-on: ubuntu-26.04
    steps:
      - uses: actions/checkout@{ci_support.PINNED_ACTIONS['actions/checkout']}
        with:
          fetch-depth: 0
          persist-credentials: false
      - uses: actions/setup-python@{ci_support.PINNED_ACTIONS['actions/setup-python']}
      - uses: astral-sh/setup-uv@{ci_support.PINNED_ACTIONS['astral-sh/setup-uv']}
      - run: clang-20 --version
      - run: xcode-select --switch /Applications/Xcode_16.4.app/Contents/Developer
      - run: echo runs-on: macos-15
      - run: echo runs-on: windows-2025
      - run: uv run --frozen python tools/ci.py policy --target linux-x86_64
      - run: uv run --frozen python tools/ci.py native-components --target linux-x86_64
      - run: uv run --frozen python tools/ci.py policy --target macos-arm64
      - run: uv run --frozen python tools/ci.py native-components --target macos-arm64
      - run: uv run --frozen python tools/ci.py policy --target windows-x86_64
      - run: uv run --frozen python tools/ci.py policy --target android-arm64
"""
    if ci_support.workflow_violations(clean):
        raise RuntimeError("ci selftest rejected the clean workflow fixture")
    dirty = clean.replace(
        f"@{ci_support.PINNED_ACTIONS['actions/checkout']}", "@v4", 1
    )
    dirty += "\n# GAME_PC_DIR: ${{ secrets.GAME_PC_DIR }}\n"
    reasons = "\n".join(ci_support.workflow_violations(dirty))
    if "exact commit" not in reasons or "game/oracle" not in reasons:
        raise RuntimeError("ci selftest did not reject unpinned actions and game inputs")
    shallow = clean.replace("fetch-depth: 0", "fetch-depth: 1", 1)
    credentialed = clean.replace("persist-credentials: false", "persist-credentials: true", 1)
    shallow_reasons = "\n".join(ci_support.workflow_violations(shallow))
    credential_reasons = "\n".join(ci_support.workflow_violations(credentialed))
    if "full history" not in shallow_reasons:
        raise RuntimeError("ci selftest accepted a shallow checkout")
    if "persist-credentials: false" not in credential_reasons:
        raise RuntimeError("ci selftest accepted a credential-persisting checkout")
    if ci_support.native_targets(ci_support.TARGETS["linux-x86_64"])[-3:] != (
        "test_x86_guest_call_stack",
        "test_jit_intercept",
        "test_x86_import_fastpath",
    ):
        raise RuntimeError("ci selftest lost the Linux JIT integration targets")
    for unsupported in ("windows-x86_64", "android-arm64"):
        try:
            ci_support.native_targets(ci_support.TARGETS[unsupported])
        except RuntimeError:
            continue
        raise RuntimeError(f"ci selftest accepted a fake {unsupported} native build")
    print("ci selftest: pinned actions, asset refusal, and support tiers distinguished")


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="command", required=True)
    for name in ("policy", "native-components"):
        command = commands.add_parser(name)
        command.add_argument("--target", required=True, choices=tuple(ci_support.TARGETS))
    commands.add_parser("selftest")
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    try:
        if args.command == "selftest":
            selftest()
            return 0
        target = target_named(args.target)
        if args.command == "policy":
            policy(target)
        else:
            native_components(target)
    except ci_support.CiFailure as error:
        print(f"ci: FAILED: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
