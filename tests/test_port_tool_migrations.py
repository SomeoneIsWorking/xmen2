#!/usr/bin/env python3
"""Discriminators for Python tools that replaced build-path shell interfaces."""

from __future__ import annotations

import unittest

from tools import native_shot


class NativeShotTests(unittest.TestCase):
    def test_valid_request(self) -> None:
        native_shot.validate_request(10, "touch-hud")

    def test_path_name_is_refused(self) -> None:
        with self.assertRaisesRegex(ValueError, "one filename"):
            native_shot.validate_request(10, "../escape")


if __name__ == "__main__":
    unittest.main()
