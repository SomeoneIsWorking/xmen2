#!/usr/bin/env python3
import sys
import unittest
from pathlib import Path


sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
import android_qualify


class AndroidQualificationTests(unittest.TestCase):
    def test_select_device_refuses_ambiguous_and_offline_devices(self):
        self.assertEqual(android_qualify.select_device(["one"], None), "one")
        with self.assertRaisesRegex(RuntimeError, "exactly one"):
            android_qualify.select_device(["one", "two"], None)
        with self.assertRaisesRegex(RuntimeError, "not an attached"):
            android_qualify.select_device(["one"], "two")

    def test_parsers_refuse_missing_measurements(self):
        self.assertEqual(android_qualify.parse_total_pss_kb("TOTAL PSS: 12,345\n"), 12345)
        with self.assertRaisesRegex(RuntimeError, "TOTAL PSS"):
            android_qualify.parse_total_pss_kb("no memory value\n")
        self.assertEqual(android_qualify.thermal_observations("Current status: 0\n"),
                         ["Current status: 0"])
        with self.assertRaisesRegex(RuntimeError, "thermal/status"):
            android_qualify.thermal_observations("nothing relevant\n")

    def test_report_requires_real_presented_samples(self):
        status = {
            "frames_presented": 100,
            "renderer_ready": True,
            "renderer_backend": "vulkan",
            "presentation_width": 1920,
            "presentation_height": 1080,
            "frame_ms_p50": 16.0,
            "frame_ms_p95": 21.0,
            "frame_ms_p99": 28.0,
            "frame_sample_count": 99,
        }
        samples = [
            {"elapsed_s": 0.0, "status": status | {"frames_presented": 1},
             "total_pss_kb": 100, "thermal": ["status: 0"]},
            {"elapsed_s": 1200.0, "status": status,
             "total_pss_kb": 125, "thermal": ["status: 1"]},
        ]
        report = android_qualify.build_report({"ro.product.model": "fixture"}, "target",
                                              sorted(android_qualify.REQUIRED_SCENARIOS), 0.0,
                                              samples, 1200.0, 4500.0, True)
        self.assertEqual(report["frames_presented_during_run"], 99)
        self.assertEqual(report["peak_total_pss_kb"], 125)
        self.assertEqual(report["renderer"]["backend"], "vulkan")
        self.assertEqual(report["final_frame_time_ms"]["p95"], 21.0)
        broken = [samples[0], samples[1] | {"status": status | {"frames_presented": 1}}]
        with self.assertRaisesRegex(RuntimeError, "did not present"):
            android_qualify.build_report({}, "target", sorted(android_qualify.REQUIRED_SCENARIOS),
                                         0.0, broken,
                                         1200.0, 4500.0, True)


if __name__ == "__main__":
    unittest.main()
