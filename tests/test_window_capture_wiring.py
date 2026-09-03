#!/usr/bin/env python3
"""Pin final-frame capture after composition/UI and before submission."""

from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[1]


class WindowCaptureWiringTest(unittest.TestCase):
    def test_final_output_order(self) -> None:
        source = (ROOT / "src/gpu/gpu_device.c").read_text()
        start = source.index("void gpu_frame_end(void)")
        end = source.index("void gpu_frame_clear(", start)
        body = source[start:end]

        operations = (
            "gpu_present_composite(g_cmd, final_output",
            "x2_ui_render(g_gpu, g_cmd, final_output",
            "gpu_capture_submit_frame(",
        )
        positions = [body.index(operation) for operation in operations]
        self.assertEqual(positions, sorted(positions))
        self.assertIn("gpu_headless_active() ? g_swap : final_output", body)
        self.assertIn("gpu_headless_active() ? NULL : g_output", body)

        capture = (ROOT / "src/gpu/gpu_capture.c").read_text()
        start = capture.index("int gpu_capture_submit_frame(")
        submit = capture[start:]
        operations = (
            "gpu_capture_frame_record(",
            "gpu_frame_submit(",
            "gpu_capture_frame_complete(",
        )
        positions = [submit.index(operation) for operation in operations]
        self.assertEqual(positions, sorted(positions))

    def test_control_capture_is_driven_by_frame_completion(self) -> None:
        capture = (ROOT / "src/gpu/gpu_capture.c").read_text()
        frame_start = capture.index("int gpu_capture_submit_frame(")
        frame = capture[frame_start:]
        self.assertLess(
            frame.index("gpu_capture_frame_complete("),
            frame.index("g_frame_observer()"),
        )

        control = (ROOT / "src/native/control.c").read_text()
        self.assertIn(
            "gpu_capture_set_frame_observer(control_frame_pump)", control
        )
        pump_start = control.index("void control_pump(")
        frame_pump_start = control.index("static void control_frame_pump(")
        guest_pump = control[pump_start:frame_pump_start]
        frame_pump_end = control.index("/* Hand work", frame_pump_start)
        frame_pump = control[frame_pump_start:frame_pump_end]
        self.assertNotIn("x2_control_screenshot_poll(", guest_pump)
        self.assertIn("x2_control_screenshot_poll(", frame_pump)


if __name__ == "__main__":
    unittest.main()
