#!/usr/bin/env python3
"""Discriminators for Python tools that replaced build-path shell interfaces."""

from __future__ import annotations

import json
from pathlib import Path
import tempfile
import unittest

from tools import native_discover
from tools import native_shot
from tools import xbox_discover
from tools import xbox_relift
from tools import xbox_run


class NativeDiscoverTests(unittest.TestCase):
    def test_target_parser_reports_unique_module_address_pairs(self) -> None:
        report = """missing constructor targets:
    XMen2.exe  0x005A43D0  3 calls
    libIGGui.dll 0x10001234 1 call
    XMen2.exe  0x005A43D0  3 calls
"""
        self.assertEqual(
            native_discover.parse_missing_targets(report),
            [("XMen2.exe", "0x005A43D0"), ("libIGGui.dll", "0x10001234")],
        )

    def test_target_parser_exposes_an_unrecognized_shape(self) -> None:
        self.assertEqual(native_discover.parse_missing_targets("XMen2.exe => 005A43D0"), [])


class NativeShotTests(unittest.TestCase):
    def test_valid_request(self) -> None:
        native_shot.validate_request(10, "touch-hud")

    def test_path_name_is_refused(self) -> None:
        with self.assertRaisesRegex(ValueError, "one filename"):
            native_shot.validate_request(10, "../escape")


class XboxDiscoverTests(unittest.TestCase):
    def test_containing_function_is_identified(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "functions.json"
            path.write_text(json.dumps([{"start": "0x1000", "end": "0x1100"}]))
            self.assertEqual(xbox_discover.containing_function(path, 0x1040),
                             (0x1000, 0x1100))

    def test_empty_function_export_is_refused(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "functions.json"
            path.write_text("[]")
            with self.assertRaisesRegex(ValueError, "no readable function records"):
                xbox_discover.containing_function(path, 0x1040)


class XboxRunTests(unittest.TestCase):
    def test_game_link_can_replace_only_a_symlink(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            run = root / "run"
            first = root / "first"
            second = root / "second"
            run.mkdir()
            first.mkdir()
            second.mkdir()
            xbox_run.publish_game_link(run, first)
            xbox_run.publish_game_link(run, second)
            self.assertEqual((run / "game").resolve(), second)

    def test_regular_game_path_is_preserved(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            run = Path(directory)
            (run / "game").write_text("owned")
            with self.assertRaisesRegex(SystemExit, "refusing to replace"):
                xbox_run.publish_game_link(run, run)
            self.assertEqual((run / "game").read_text(), "owned")


class XboxReliftTests(unittest.TestCase):
    def test_override_wraps_are_generated_from_the_isolated_entries(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            overrides = root / "overrides.json"
            generated = root / "generated"
            generated.mkdir()
            overrides.write_text(json.dumps([{"start": "0x00401000", "name": "menu"}]))
            (generated / "recomp_iso_00401000.c").write_text("isolated")
            xbox_relift.write_override_wraps(overrides, generated)
            contents = (generated / "recomp_overrides.cmake").read_text()
            self.assertIn("-Wl,--wrap=sub_00401000", contents)

    def test_runtime_seed_missing_from_dispatch_is_refused(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            generated = Path(directory)
            (generated / "recomp_dispatch.c").write_text("0x00400000")
            with self.assertRaisesRegex(SystemExit, "runtime-observed seed"):
                xbox_relift.verify_seed_dispatch([{"start": "0x00401000"}], generated)


if __name__ == "__main__":
    unittest.main()
