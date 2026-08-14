#!/usr/bin/env python3
"""Ratchet touched legacy monoliths; keep new ownership modules small."""
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1]
LIMITS = {
    "tools/recomp.py": 3199,
    "src/app/x2run.c": 244,
    "src/app/x2run_diag.c": 100,
    "src/recomp/x87crt.c": 150,
    "src/recomp/x87host.c": 80,
    "src/recomp/x86callbacks.c": 80,
    "src/x86watch.c": 390,
    "src/x86watch_memory.c": 80,
    "src/x86watch_stack.c": 80,
    "src/x86watch_trace.c": 80,
    "src/native/dinput_device.c": 620,
    "src/native/dinput_system.c": 220,
    "src/native/dinput_script.c": 200,
    "src/native/dinput_joystick.c": 150,
    "src/native/env_file.c": 180,
    "src/native/pad_glyphs.c": 140,
    "src/native/x2native_options.c": 80,
    "tools/extract_fb.py": 180,
    "tools/prepare_native_assets.py": 170,
    "tools/pad_glyph_manifest.py": 90,
    "tools/recomp_hosted.py": 100,
    "tools/recomp_host_call.py": 80,
}


class StructureRatchet(unittest.TestCase):
    def test_touched_files_do_not_grow_into_monoliths(self):
        failures = []
        for name, limit in LIMITS.items():
            path = ROOT / name
            count = len(path.read_text().splitlines())
            if count > limit:
                failures.append("%s: %d lines, limit %d" % (name, count, limit))
        self.assertFalse(failures, "extract a cohesive module:\n" + "\n".join(failures))


if __name__ == "__main__":
    unittest.main()
