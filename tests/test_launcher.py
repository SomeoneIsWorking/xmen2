#!/usr/bin/env python3
"""Cold-path launcher contract tests with no game launch or network access."""

from __future__ import annotations

import importlib.util
import os
from pathlib import Path
import subprocess
import sys
import unittest
from unittest import mock


ROOT = Path(__file__).resolve().parent.parent


def load_module(name: str, path: Path):
    spec = importlib.util.spec_from_file_location(name, path)
    if not spec or not spec.loader:
        raise RuntimeError(f"could not load {path}")
    module = importlib.util.module_from_spec(spec)
    sys.modules[name] = module
    spec.loader.exec_module(module)
    return module


bootstrap = load_module("x2_bootstrap_test", ROOT / "bootstrap.py")
runner = load_module("x2_run_test", ROOT / "tools" / "run.py")


class LauncherContract(unittest.TestCase):
    def test_shell_is_only_a_no_argument_uv_shim(self):
        text = (ROOT / "run.sh").read_text()
        self.assertLessEqual(len(text.splitlines()), 15)
        self.assertIn("exec uv run --frozen python bootstrap.py", text)
        self.assertNotIn('"$@"', text)
        self.assertNotIn(" wine", text)
        self.assertNotIn(" stock", text)

        refused = subprocess.run([str(ROOT / "run.sh"), "wine"], cwd=ROOT,
                                 text=True, stdout=subprocess.PIPE,
                                 stderr=subprocess.STDOUT)
        self.assertEqual(refused.returncode, 2)
        self.assertIn("takes no arguments", refused.stdout)
        self.assertNotIn("game identity", refused.stdout)

    def test_bootstrap_execs_locked_interpreter_without_arguments(self):
        with mock.patch.object(bootstrap, "initialize"), \
             mock.patch.object(bootstrap.os, "execve") as execute, \
             mock.patch.object(bootstrap.sys, "argv", ["bootstrap.py"]):
            self.assertEqual(bootstrap.main(), 1)
        argv = execute.call_args.args[1]
        self.assertEqual(argv, [sys.executable, str(ROOT / "tools" / "run.py")])
        self.assertEqual(execute.call_args.args[0], sys.executable)

    def test_bootstrap_refuses_arguments_before_initializing(self):
        with mock.patch.object(bootstrap, "initialize") as initialize, \
             mock.patch.object(bootstrap.sys, "argv", ["bootstrap.py", "wine"]):
            with self.assertRaisesRegex(SystemExit, "takes no arguments"):
                bootstrap.main()
        initialize.assert_not_called()

    def test_runner_has_no_mode_or_argument_surface(self):
        text = (ROOT / "tools" / "run.py").read_text()
        for retired in ("RUN_ARGS", "run_wine", "require_command", 'mode = "native"'):
            self.assertNotIn(retired, text)

    def test_runner_prefers_clang_and_accepts_gcc(self):
        common = {"cmake": "/uv/cmake", "ninja": "/uv/ninja"}

        def clang_which(name: str) -> str | None:
            return common.get(name) or ({"clang": "/usr/bin/clang",
                                         "clang++": "/usr/bin/clang++"}.get(name))

        clang = runner.resolve_toolchain(clang_which, {})
        self.assertEqual((clang.cxx, clang.cmake_compiler_id),
                         ("/usr/bin/clang++", "Clang"))

        def gcc_which(name: str) -> str | None:
            return common.get(name) or ({"gcc": "/usr/bin/gcc",
                                         "g++": "/usr/bin/g++"}.get(name))

        gcc = runner.resolve_toolchain(gcc_which, {})
        self.assertEqual((gcc.cxx, gcc.cmake_compiler_id), ("/usr/bin/g++", "GNU"))

    def test_runner_honours_an_explicit_compiler_pair(self):
        tools = {"cmake": "/uv/cmake", "ninja": "/uv/ninja",
                 "mycc": "/opt/toolchain/mycc", "mycxx": "/opt/toolchain/mycxx"}
        selected = runner.resolve_toolchain(tools.get, {"CC": "mycc", "CXX": "mycxx"})
        self.assertEqual((selected.cc, selected.cxx),
                         ("/opt/toolchain/mycc", "/opt/toolchain/mycxx"))

    def test_committed_exports_are_complete_metadata_without_encodings(self):
        expected = {f"{module}.json" for module in bootstrap.modules()}
        actual = {path.name for path in (ROOT / "re" / "ghidra").glob("*.json")}
        self.assertEqual(actual, expected)
        for path in sorted((ROOT / "re" / "ghidra").glob("*.json")):
            self.assertGreater(path.stat().st_size, 0)
            self.assertFalse(bootstrap.has_instruction_encodings(path), path)

    def test_locked_environment_owns_resvg(self):
        project = (ROOT / "pyproject.toml").read_text()
        lock = (ROOT / "uv.lock").read_text()
        self.assertIn('"resvg-py==', project)
        self.assertIn('name = "resvg-py"', lock)


if __name__ == "__main__":
    os.chdir(ROOT)
    unittest.main()
