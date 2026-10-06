#!/usr/bin/env python3
"""Non-launching tests for the Python libIG shim builder."""

from __future__ import annotations

from pathlib import Path
import tempfile
import unittest

from tools import build_shim


class BuildShimTests(unittest.TestCase):
    def test_target_validation_accepts_named_dll(self) -> None:
        self.assertEqual(
            build_shim.validate_target("trace", "libIGDisplay.dll"),
            ("libIGDisplay", "libIGDisplay_orig"),
        )

    def test_target_validation_refuses_modes_and_paths(self) -> None:
        for mode, dll in (
            ("other", "libIGDisplay.dll"),
            ("trace", "../libIGDisplay.dll"),
            ("proxy", "libIGDisplay.so"),
        ):
            with self.subTest(mode=mode, dll=dll):
                with self.assertRaises(build_shim.BuildRefusal):
                    build_shim.validate_target(mode, dll)

    def test_paths_keep_build_products_out_of_scratch(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            game = root / "game"
            game.mkdir()
            (game / "libIGDisplay.dll").write_bytes(b"source")
            paths = build_shim.configured_paths(
                root,
                "proxy",
                "libIGDisplay.dll",
                {"GAME_PC_DIR": str(game)},
            )
            self.assertEqual(paths["build"], root / "build/shim/proxy/libIGDisplay")
            self.assertEqual(paths["run"], root / "scratch/run/proxy")

    def test_stage_preserves_install_and_replaces_owned_files(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            game = root / "game"
            build_dir = root / "build/shim/proxy/libIGDisplay"
            game.mkdir(parents=True)
            build_dir.mkdir(parents=True)
            (game / "libIGDisplay.dll").write_bytes(b"original")
            (game / "XMen2.exe").write_bytes(b"game")
            (game / "alchemy.ini").write_text("multiSampleType = 4\n")
            (build_dir / "libIGDisplay.dll").write_bytes(b"proxy")
            paths = build_shim.configured_paths(
                root,
                "proxy",
                "libIGDisplay.dll",
                {"GAME_PC_DIR": str(game)},
            )
            build_shim.stage_run(paths, "libIGDisplay.dll")
            run = paths["run"]
            self.assertTrue((run / "XMen2.exe").is_symlink())
            self.assertEqual((run / "libIGDisplay.dll").read_bytes(), b"proxy")
            self.assertEqual((run / "libIGDisplay_orig.dll").read_bytes(), b"original")
            self.assertIn("multiSampleType = 0", (run / "alchemy.ini").read_text())
            self.assertEqual((game / "libIGDisplay.dll").read_bytes(), b"original")


if __name__ == "__main__":
    unittest.main()
