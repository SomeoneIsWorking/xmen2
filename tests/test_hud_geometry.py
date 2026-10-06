#!/usr/bin/env python3
"""Negative controls for the HUD asset-to-header measurement discriminator."""

from pathlib import Path
import sys
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
import hud_geometry


class HudGeometryTests(unittest.TestCase):
    def setUp(self):
        self.source = hud_geometry.HEADER.read_text()
        self.constants = hud_geometry.parse_constants(self.source)

    def test_real_header_has_complete_measurement_contract(self):
        self.assertEqual(self.constants.keys(), hud_geometry.CONSTANT_NAMES)

    def test_parser_refuses_missing_duplicate_and_expression(self):
        with self.assertRaisesRegex(ValueError, "missing header constants"):
            hud_geometry.parse_constants("")
        with self.assertRaisesRegex(ValueError, "duplicate"):
            hud_geometry.parse_constants(self.source + "\n#define HUD_PORTRAIT_EXTENT (2.0f)\n")
        with self.assertRaisesRegex(ValueError, "invalid"):
            hud_geometry.parse_constants(self.source.replace("(61.0f)", "(60.0f + 1.0f)"))

    def test_portrait_discriminator_accepts_match_and_refuses_changed_measurement(self):
        extent = self.constants["HUD_PORTRAIT_EXTENT"]
        matching = ((-extent, 0.0, -extent), (extent, 0.0, extent))
        self.assertEqual(hud_geometry.check_bounds("portrait", matching, self.constants, None), 4)
        changed = (matching[0], (extent + 1.0, 0.0, extent))
        with self.assertRaisesRegex(ValueError, "portrait: MAX_X measured.*checked 2/4"):
            hud_geometry.check_bounds("portrait", changed, self.constants, None)

    def test_frame_discriminator_checks_both_axes(self):
        prefix = "HUD_HEALTH_FRAME"
        bounds = ((-61.0, 0.0, -15.0), (61.0, 0.0, 15.0))
        self.assertEqual(hud_geometry.check_bounds("panel", bounds, self.constants, prefix), 4)
        with self.assertRaisesRegex(ValueError, "panel: MAX_Z measured"):
            hud_geometry.check_bounds(
                "panel", (bounds[0], (61.0, 0.0, 16.0)), self.constants, prefix
            )

    def test_empty_portrait_set_is_not_evidence(self):
        with patch.object(Path, "is_dir", return_value=True):
            with patch.object(Path, "glob", return_value=iter(())):
                with self.assertRaisesRegex(ValueError, "matched 0; no portrait evidence"):
                    hud_geometry.verify_install(Path("install"), self.constants, lambda path: None)

    def test_missing_corpus_cannot_pass_silently(self):
        with self.assertRaisesRegex(ValueError, "scanned 0 assets"):
            hud_geometry.verify_install(
                hud_geometry.HEADER / "not-a-directory", self.constants, lambda path: None
            )


if __name__ == "__main__":
    unittest.main()
