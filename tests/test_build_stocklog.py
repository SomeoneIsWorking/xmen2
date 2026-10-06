#!/usr/bin/env python3
"""Non-game-launching tests for tools/build_stocklog.py."""

from __future__ import annotations

from pathlib import Path
import tempfile
import unittest

from tools import build_stocklog

SCRATCH = Path(__file__).resolve().parents[1] / "scratch"


def scratch_temporary_directory() -> tempfile.TemporaryDirectory[str]:
    SCRATCH.mkdir(parents=True, exist_ok=True)
    return tempfile.TemporaryDirectory(dir=SCRATCH)


class BuildStocklogTests(unittest.TestCase):
    def test_default_run_name_is_stocklog(self) -> None:
        self.assertEqual(build_stocklog.parse_args([]).run_name, "stocklog")

    def test_definition_rewrite_keeps_all_other_exports(self) -> None:
        generated = """LIBRARY d3d8
EXPORTS
  "DebugSetMute" = "d3d8_real.DebugSetMute"
  "Direct3DCreate8" = "d3d8_real.Direct3DCreate8"
"""
        rewritten, forward_count = build_stocklog.rewrite_proxy_definition(generated)
        self.assertEqual(forward_count, 1)
        self.assertIn("Direct3DCreate8 = Direct3DCreate8@4", rewritten)
        self.assertIn('"DebugSetMute" = "d3d8_real.DebugSetMute"', rewritten)

    def test_definition_rewrite_refuses_changed_generator_format(self) -> None:
        generated = """LIBRARY d3d8
EXPORTS
  Direct3DCreate8=d3d8_real.Direct3DCreate8
"""
        with self.assertRaisesRegex(build_stocklog.BuildRefusal, "format changed"):
            build_stocklog.rewrite_proxy_definition(generated)

    def test_compile_sources_are_explicit_and_include_shadow_trace(self) -> None:
        root = Path("/repo")
        command = build_stocklog.compile_command(
            root, root / "build/proxy/d3d8.dll", root / "proxy.def"
        )
        source_args = [arg for arg in command if arg.endswith((".c", ".S"))]
        self.assertEqual(
            source_args,
            [str(root / source) for source in build_stocklog.PROXY_SOURCES],
        )
        self.assertIn(str(root / "tools/proxy_d3d8/shadow_trace.c"), command)
        self.assertIn(str(root / "tools/proxy_d3d8/shadow_setting.c"), command)

    def test_ordinal_import_counter_distinguishes_named_imports(self) -> None:
        imports = """d3d8.dll Direct3DCreate8
d3d8.dll @7
kernel32.dll @12
d3d8.dll @19
"""
        self.assertEqual(build_stocklog.ordinal_d3d8_import_count(imports), 2)

    def test_run_name_refuses_escape(self) -> None:
        for name in (
            "",
            ".",
            "../stocklog",
            "/stocklog",
            "nested/stocklog",
            "nested/../../stocklog",
        ):
            with self.subTest(name=name):
                with self.assertRaises(build_stocklog.BuildRefusal):
                    build_stocklog.validate_run_name(name)

    def test_missing_game_configuration_refuses_before_build(self) -> None:
        with scratch_temporary_directory() as temporary:
            root = Path(temporary)
            with self.assertRaisesRegex(build_stocklog.BuildRefusal, "GAME_PC_DIR"):
                build_stocklog.configured_paths(root, "stocklog", {})

    def test_relative_environment_paths_are_rooted_at_repository(self) -> None:
        with scratch_temporary_directory() as temporary:
            root = Path(temporary)
            (root / "game").mkdir()
            paths = build_stocklog.configured_paths(
                root,
                "stocklog",
                {"GAME_PC_DIR": "game", "WINE_PREFIX": "wine"},
            )
            self.assertEqual(paths["game"], root / "game")
            self.assertEqual(
                paths["real"],
                root / "wine/drive_c/windows/syswow64/d3d8.dll",
            )

    def test_environment_reader_handles_tracked_example_syntax(self) -> None:
        with scratch_temporary_directory() as temporary:
            env_file = Path(temporary) / ".env"
            env_file.write_text(
                '# comment\nexport GAME_PC_DIR="a path" # comment\nWINE_PREFIX=/wine\n'
            )
            self.assertEqual(
                build_stocklog._read_env(env_file),
                {"GAME_PC_DIR": "a path", "WINE_PREFIX": "/wine"},
            )

    def test_stage_run_uses_symlinks_and_replaces_only_owned_files(self) -> None:
        with scratch_temporary_directory() as temporary:
            root = Path(temporary)
            game = root / "game"
            run = root / "scratch/run/stocklog"
            game.mkdir()
            (game / "XMen2.exe").write_bytes(b"fixture executable")
            (game / "Data").mkdir()
            (game / "d3d8.dll").write_bytes(b"install must remain unchanged")
            (game / "alchemy.ini").write_text("multiSampleType = 4\notherSetting = 1\n")
            real = root / "real.dll"
            proxy = root / "proxy.dll"
            real.write_bytes(b"real")
            proxy.write_bytes(b"proxy")
            run.mkdir(parents=True)
            (run / "d3d8.dll").symlink_to(game / "d3d8.dll")
            (run / "d3d8_shadow_trace.jsonl").write_text("stale evidence\n")

            build_stocklog.stage_run(game, run, real, proxy)

            self.assertTrue((run / "XMen2.exe").is_symlink())
            self.assertTrue((run / "Data").is_symlink())
            self.assertEqual((run / "d3d8_real.dll").read_bytes(), b"real")
            self.assertEqual((run / "d3d8.dll").read_bytes(), b"proxy")
            self.assertEqual((game / "d3d8.dll").read_bytes(), b"install must remain unchanged")
            self.assertIn("multiSampleType = 0", (run / "alchemy.ini").read_text())
            self.assertFalse((run / "d3d8_shadow_trace.jsonl").exists())

    def test_stage_run_refuses_to_replace_unowned_directory(self) -> None:
        with scratch_temporary_directory() as temporary:
            root = Path(temporary)
            game = root / "game"
            run = root / "run"
            game.mkdir()
            run.mkdir()
            (game / "Data").mkdir()
            (game / "alchemy.ini").write_text("multiSampleType = 4\n")
            (run / "Data").mkdir()
            real = root / "real.dll"
            proxy = root / "proxy.dll"
            real.write_bytes(b"real")
            proxy.write_bytes(b"proxy")

            with self.assertRaisesRegex(build_stocklog.BuildRefusal, "directory"):
                build_stocklog.stage_run(game, run, real, proxy)


if __name__ == "__main__":
    unittest.main()
