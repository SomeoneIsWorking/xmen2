#!/usr/bin/env python3
"""Cold-path launcher contract tests with no game launch or network access."""

from __future__ import annotations

import importlib.util
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest import mock


ROOT = Path(__file__).resolve().parent.parent
SCRATCH = ROOT / "scratch/tests"


def scratch_directory():
    SCRATCH.mkdir(parents=True, exist_ok=True)
    return tempfile.TemporaryDirectory(dir=SCRATCH)


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
    def test_shell_refuses_arguments(self):
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

    def test_player_bootstrap_excludes_maintainer_only_re_harness(self):
        self.assertEqual({repo.name for repo in bootstrap.SHARED_REPOS},
                         {"alchemy", "android-port", "web-port", "port-assets",
                          "jit-common", "x86port"})

    def test_bootstrap_finds_repository_local_game(self):
        for relative in (Path("."), Path("X-Men Legends II")):
            with self.subTest(relative=relative), scratch_directory() as raw:
                root = Path(raw)
                game = root / relative
                game.mkdir(parents=True, exist_ok=True)
                (game / "XMen2.exe").write_bytes(b"fixture executable")
                with mock.patch.object(bootstrap, "ROOT", root), \
                     mock.patch.dict(bootstrap.os.environ, {}, clear=True):
                    self.assertEqual(bootstrap.find_game(), game.resolve())
                    self.assertEqual(bootstrap.os.environ["GAME_PC_DIR"],
                                     str(game.resolve()))

    def test_bootstrap_refuses_ambiguous_repository_local_games(self):
        with scratch_directory() as raw:
            root = Path(raw)
            for name in ("disc-one", "disc-two"):
                game = root / name
                game.mkdir()
                (game / "XMen2.exe").write_bytes(b"fixture executable")
            with mock.patch.object(bootstrap, "ROOT", root), \
                 mock.patch.dict(bootstrap.os.environ, {}, clear=True), \
                 self.assertRaisesRegex(SystemExit, "multiple repository-local"):
                bootstrap.find_game()

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

    def test_homebrew_vulkan_pkg_config_satisfies_dependency_probe(self):
        toolchain = runner.Toolchain("cmake", "ninja", "clang", "clang++", "Clang")
        with mock.patch.object(runner.shutil, "which",
                               side_effect=lambda name: f"/opt/homebrew/bin/{name}"), \
             mock.patch.object(runner, "command_ok", return_value=True), \
             mock.patch.object(runner.ctypes.util, "find_library", return_value=None):
            runner.check_native_dependencies(toolchain)

    def test_existing_checkout_must_already_be_at_the_pin(self):
        repo = bootstrap.SharedRepo("fixture", "https://github.com/example/fixture.git",
                                    "a" * 40, "marker")
        with scratch_directory() as raw:
            target = Path(raw) / "fixture"
            (target / ".git").mkdir(parents=True)
            (target / "marker").write_text("marker")
            local_ref = target / ".git/valuable-local-ref"
            local_ref.write_text("must survive")

            # url, status, HEAD, submodule status
            pinned = [repo.url, "", repo.revision, ""]
            with mock.patch.object(bootstrap, "run_git", side_effect=pinned):
                bootstrap.validate_checkout(repo, target)

            outdated = [repo.url, "", "b" * 40, ""]
            with mock.patch.object(bootstrap, "run_git", side_effect=outdated):
                with self.assertRaisesRegex(SystemExit, "move it aside"):
                    bootstrap.validate_checkout(repo, target)
            self.assertEqual(local_ref.read_text(), "must survive")

            # A pin covers the submodules too: x86port's Zydis is one, and an
            # uninitialized one validates clean and then fails in CMake.
            missing = [repo.url, "", repo.revision,
                       "-0000000000000000000000000000000000000000 vendor/zydis"]
            with mock.patch.object(bootstrap, "run_git", side_effect=missing):
                with self.assertRaisesRegex(SystemExit, "vendor/zydis"):
                    bootstrap.validate_checkout(repo, target)

    def test_a_launcher_clone_follows_a_pin_bump(self):
        """`git pull && ./run.sh` after a pin bump moves vendor/shared itself."""
        def git(cwd: Path, *arguments: str) -> str:
            return subprocess.run(["git", "-c", "user.name=t", "-c", "user.email=t@t",
                                   "-c", "commit.gpgsign=false", *arguments],
                                  cwd=cwd, check=True, text=True,
                                  capture_output=True).stdout.strip()

        with scratch_directory() as raw:
            upstream = Path(raw) / "upstream"
            upstream.mkdir()
            git(upstream, "init", "--quiet", "--initial-branch=main")
            (upstream / "marker").write_text("1")
            git(upstream, "add", "marker")
            git(upstream, "commit", "--quiet", "-m", "old pin")
            old = git(upstream, "rev-parse", "HEAD")
            (upstream / "marker").write_text("2")
            git(upstream, "commit", "--quiet", "-am", "new pin")
            new = git(upstream, "rev-parse", "HEAD")
            repo = bootstrap.SharedRepo("fixture", str(upstream), new, "marker")

            clone = Path(raw) / "clone"
            git(Path(raw), "clone", "--quiet", str(upstream), str(clone))
            git(clone, "checkout", "--quiet", "--detach", old)
            # Untracked work: git itself would carry it across the checkout.
            (clone / "notes").write_text("unsaved")
            with self.assertRaisesRegex(SystemExit, "local changes"):
                bootstrap.advance_clone(repo, clone)
            self.assertEqual(git(clone, "rev-parse", "HEAD"), old)
            (clone / "notes").unlink()

            git(clone, "checkout", "--quiet", "-b", "mine")
            (clone / "marker").write_text("mine")
            git(clone, "commit", "--quiet", "-am", "local only")
            mine = git(clone, "rev-parse", "HEAD")
            with self.assertRaisesRegex(SystemExit, "no branch"):
                bootstrap.advance_clone(repo, clone)
            self.assertEqual(git(clone, "rev-parse", "HEAD"), mine)

            git(clone, "checkout", "--quiet", "--detach", old)
            self.assertTrue(bootstrap.advance_clone(repo, clone))
            self.assertEqual(git(clone, "rev-parse", "HEAD"), new)
            bootstrap.validate_checkout(repo, clone)
            self.assertFalse(bootstrap.advance_clone(repo, clone))

            other = bootstrap.SharedRepo("fixture", "https://github.com/example/other.git",
                                         new, "marker")
            with self.assertRaisesRegex(SystemExit, "refusing to mutate"):
                bootstrap.advance_clone(other, clone)

    def test_a_configured_checkout_is_never_moved(self):
        repo = bootstrap.SharedRepo("fixture", "https://github.com/example/fixture.git",
                                    "a" * 40, "marker")
        with scratch_directory() as raw, \
             mock.patch.object(bootstrap, "SHARED_REPOS", [repo]), \
             mock.patch.dict(os.environ, {"FIXTURE_DIR": raw}), \
             mock.patch.object(bootstrap, "advance_clone") as advance, \
             mock.patch.object(bootstrap, "validate_checkout",
                               side_effect=SystemExit("move it aside")):
            with self.assertRaisesRegex(SystemExit, "move it aside"):
                bootstrap.ensure_shared()
            advance.assert_not_called()

    def test_atomic_text_publication_preserves_old_value_on_failure(self):
        with scratch_directory() as raw:
            target = Path(raw) / "cache.txt"
            target.write_text("old\n")
            with mock.patch.object(Path, "replace", autospec=True,
                                   side_effect=OSError("fixture failure")):
                with self.assertRaisesRegex(OSError, "fixture failure"):
                    bootstrap.publish_text(target, "new\n")
            self.assertEqual(target.read_text(), "old\n")
            self.assertEqual(list(target.parent.glob(".cache.txt-*")), [])

if __name__ == "__main__":
    os.chdir(ROOT)
    unittest.main()
