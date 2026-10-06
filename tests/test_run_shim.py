from __future__ import annotations

from pathlib import Path
import tempfile
import unittest

from tools.build_stocklog import BuildRefusal
from tools.run_shim import parse_key_events, resolve_paths


class RunShimTest(unittest.TestCase):
    def test_key_schedule_supports_instants_and_bounded_windows(self) -> None:
        self.assertEqual(
            parse_key_events("4:Return,10-16/3:space"),
            [(4.0, "Return"), (10.0, "space"), (13.0, "space"), (16.0, "space")],
        )

    def test_key_schedule_rejects_non_progressing_window(self) -> None:
        with self.assertRaisesRegex(BuildRefusal, "invalid X2_KEYS"):
            parse_key_events("4-10/0:Return")

    def test_run_directory_must_contain_exact_executable(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            (root / "scratch" / "run" / "stock").mkdir(parents=True)
            with self.assertRaisesRegex(BuildRefusal, "ran NOTHING"):
                resolve_paths(root, "stock", "XMen2.exe")

    def test_run_directory_name_cannot_escape_scratch(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            with self.assertRaisesRegex(BuildRefusal, "one path component"):
                resolve_paths(Path(temporary), "../stock", "XMen2.exe")


if __name__ == "__main__":
    unittest.main()
