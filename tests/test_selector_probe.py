import unittest

from tools import selector_probe


def meta(width=128, height=32):
    return {
        "event": "meta",
        "version": selector_probe.FORMAT_VERSION,
        "target": selector_probe.TARGET,
        "texture_width": width,
        "texture_height": height,
        "identity_claim": False,
        "fingerprint_algorithm": selector_probe.FINGERPRINT_ALGORITHM,
        "fingerprint_scope": selector_probe.FINGERPRINT_SCOPE,
    }


def candidate(frame=4, order=1, fingerprint="0123456789abcdef"):
    value = {
        "event": "candidate",
        "frame": frame,
        "order": order,
        "texture_guest": "12345678",
        "texture_resolved": True,
        "texture_width": 128,
        "texture_height": 32,
        "texture_format": 21,
        "texture_levels": 1,
        "texture_faces": 1,
        "fingerprint_available": fingerprint is not None,
        "fingerprint": fingerprint,
        "fingerprint_revision": 1 if fingerprint is not None else 0,
        "primitive": 5,
        "primitive_count": 4,
        "elements": 6,
        "indexed": False,
        "fvf": "00000144",
        "layout_valid": True,
        "vertex_stride": 24,
        "position_offset": 0,
        "pretransformed": False,
        "vertex_bytes": 144,
        "index_bytes": 0,
        "bounds_valid": True,
        "min_x": 10.0,
        "min_y": 20.0,
        "min_z": 0.25,
        "max_x": 100.0,
        "max_y": 48.0,
        "max_z": 0.75,
        "elements_requested": 6,
        "elements_used": 6,
        "behind": 0,
        "out_of_range": 0,
        "unavailable": 0,
        "position_samples": [["00000000"] * 3 for _ in range(6)],
        "positions_truncated": False,
    }
    value.update({field: 0 for field in selector_probe.STATE_FIELDS})
    return value


def result(frame=4, order=1, accepted=True):
    return {
        "event": "result",
        "frame": frame,
        "order": order,
        "accepted": accepted,
    }


class SelectorProbeTest(unittest.TestCase):
    def test_summary_preserves_both_build_answers(self):
        records = [
            meta(),
            candidate(order=1),
            result(order=1, accepted=True),
            candidate(order=2, fingerprint=None),
            result(order=2, accepted=False),
        ]
        summary = selector_probe.summarize(records)
        self.assertEqual(len(summary.candidates), 2)
        self.assertEqual(len(summary.accepted), 1)
        self.assertEqual(len(summary.refused), 1)

    def test_summary_refuses_no_reach(self):
        with self.assertRaises(selector_probe.Refuse):
            selector_probe.summarize([meta()])

    def test_summary_refuses_missing_result(self):
        with self.assertRaises(selector_probe.Refuse):
            selector_probe.summarize([meta(), candidate()])

    def test_summary_refuses_orphan_result(self):
        with self.assertRaises(selector_probe.Refuse):
            selector_probe.summarize([meta(), candidate(), result(order=2)])

    def test_summary_refuses_invented_fingerprint(self):
        wrong = candidate(fingerprint=None)
        wrong["fingerprint"] = "0123456789abcdef"
        with self.assertRaises(selector_probe.Refuse):
            selector_probe.summarize([meta(), wrong, result()])

    def test_summary_refuses_changed_geometry_denominator(self):
        wrong = candidate()
        wrong["elements_requested"] = 5
        wrong["elements_used"] = 5
        with self.assertRaises(selector_probe.Refuse):
            selector_probe.summarize([meta(), wrong, result()])

    def test_summary_refuses_identity_claim(self):
        wrong = meta()
        wrong["identity_claim"] = True
        with self.assertRaises(selector_probe.Refuse):
            selector_probe.summarize([wrong, candidate(), result()])

    def test_summary_uses_echoed_runtime_dimensions(self):
        with self.assertRaises(selector_probe.Refuse):
            selector_probe.summarize([meta(64, 64), candidate(), result()])


if __name__ == "__main__":
    unittest.main()
