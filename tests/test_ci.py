from __future__ import annotations

import importlib.util
from pathlib import Path
import sys

import pytest


ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))


def load_module(name: str, path: Path):
    spec = importlib.util.spec_from_file_location(name, path)
    assert spec is not None and spec.loader is not None
    module = importlib.util.module_from_spec(spec)
    sys.modules[name] = module
    spec.loader.exec_module(module)
    return module


ci_support = load_module("x2_ci_support_test", ROOT / "tools/ci_support.py")


def test_support_matrix_distinguishes_product_and_policy_targets():
    assert ci_support.TARGETS["linux-x86_64"].gameplay_jit
    assert ci_support.TARGETS["macos-arm64"].native_components
    assert not ci_support.TARGETS["macos-arm64"].gameplay_jit
    assert not ci_support.TARGETS["windows-x86_64"].native_components
    assert not ci_support.TARGETS["android-arm64"].native_components


def test_unsupported_targets_cannot_acquire_a_fake_native_plan():
    for name in ("windows-x86_64", "android-arm64"):
        with pytest.raises(RuntimeError, match="policy only"):
            ci_support.native_targets(ci_support.TARGETS[name])


def test_linux_native_plan_includes_real_jit_integration_boundaries():
    targets = ci_support.native_targets(ci_support.TARGETS["linux-x86_64"])
    assert "test_x86_guest_call_stack" in targets
    assert "test_jit_intercept" in targets
    assert "test_x86_import_fastpath" in targets
    assert "x86_guest_call_stack" in ci_support.native_test_regex(
        ci_support.TARGETS["linux-x86_64"]
    )


def test_workflow_policy_rejects_mutable_actions_and_game_inputs():
    fixture = """
uses: actions/checkout@v4
env:
  GAME_PC_DIR: ${{ secrets.GAME_PC_DIR }}
"""
    reasons = "\n".join(ci_support.workflow_violations(fixture))
    assert "exact commit" in reasons
    assert "game/oracle" in reasons


def test_workflow_policy_rejects_shallow_and_credentialed_checkouts():
    checkout = ci_support.PINNED_ACTIONS["actions/checkout"]
    fixture = f"""
steps:
  - uses: actions/checkout@{checkout}
    with:
      fetch-depth: 1
      persist-credentials: true
"""
    reasons = "\n".join(ci_support.checkout_policy_violations(fixture))
    assert "full history" in reasons
    assert "persist-credentials: false" in reasons


def test_workflow_policy_accepts_exact_actions_and_complete_target_calls():
    fixture = f"""
UV_VERSION: "0.10.12"
PYTHON_VERSION: "3.13"
runs-on: ubuntu-26.04
runs-on: macos-15
runs-on: windows-2025
run: clang-20 --version
run: xcode-select --switch /Applications/Xcode_16.4.app/Contents/Developer
steps:
  - uses: actions/checkout@{ci_support.PINNED_ACTIONS['actions/checkout']}
    with:
      fetch-depth: 0
      persist-credentials: false
  - uses: actions/setup-python@{ci_support.PINNED_ACTIONS['actions/setup-python']}
  - uses: astral-sh/setup-uv@{ci_support.PINNED_ACTIONS['astral-sh/setup-uv']}
run: tools/ci.py policy --target linux-x86_64
run: tools/ci.py native-components --target linux-x86_64
run: tools/ci.py policy --target macos-arm64
run: tools/ci.py native-components --target macos-arm64
run: tools/ci.py policy --target windows-x86_64
run: tools/ci.py policy --target android-arm64
"""
    assert ci_support.workflow_violations(fixture) == []


def test_repository_workflow_passes_production_policy():
    workflow = (ROOT / ".github/workflows/asset-free.yml").read_text(encoding="utf-8")
    assert ci_support.workflow_violations(workflow) == []


def test_readme_support_matrix_tracks_the_ci_authority():
    readme = (ROOT / "README.md").read_text(encoding="utf-8")
    for row in ci_support.markdown_support_rows():
        assert row in readme
