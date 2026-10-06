"""The Zen driver's reader of the product's per-heartbeat frame-time windows.

Paired cases: a run whose gameplay windows are reported, and the runs the
reader must refuse -- a silent browser, a heartbeat without the window line,
and windows that all fall inside the warm-up.
"""

import io
import unittest
from contextlib import redirect_stderr, redirect_stdout

from tools.zen_play import summarize, windows


def heartbeat(elapsed: float, presents: int, delta: int, p50: float, p95: float,
              p99: float, frames: int) -> str:
    return "\n".join([
        f"[HB] {elapsed:6.1f}s  crossings 100 (+10)",
        f"scenes 1 (+1)  clears 1 (+1)  draws 1 (+1)  presents {presents} (+{delta})",
        f"[HB]           frame ms p50 {p50:.2f} p95 {p95:.2f} p99 {p99:.2f} "
        f"over {frames} interval(s) since the last heartbeat",
    ])


def run_log(count: int, start: float = 5.0) -> str:
    lines = []
    presents = 0
    for index in range(count):
        presents += 40
        lines.append(heartbeat(start + 5.0 * index, presents, 40,
                               120.0 + index, 140.0, 150.0 + index, 40))
    return "\n".join(lines)


def quiet(text: str, warmup: float) -> tuple[int, str, str]:
    out, err = io.StringIO(), io.StringIO()
    with redirect_stdout(out), redirect_stderr(err):
        code = summarize(text, warmup, 3)
    return code, out.getvalue(), err.getvalue()


class ZenPlayTest(unittest.TestCase):
    def test_windows_pair_elapsed_with_percentiles(self) -> None:
        found = windows(run_log(3))
        self.assertEqual([w[0] for w in found], [5.0, 10.0, 15.0])
        self.assertEqual(found[1][1:], (121.0, 140.0, 151.0, 40))

    def test_summary_skips_the_warm_up(self) -> None:
        code, out, _ = quiet(run_log(10), 20.0)
        self.assertEqual(code, 0)
        self.assertIn("over 7 window(s), 280 frames", out)
        self.assertIn("worst p99 159.00 ms", out)
        self.assertIn("steady rate", out)

    def test_refuses_a_silent_browser(self) -> None:
        code, _, err = quiet("", 0.0)
        self.assertEqual(code, 1)
        self.assertIn("printed nothing", err)

    def test_refuses_a_heartbeat_without_the_window_line(self) -> None:
        text = "\n".join(line for line in run_log(8).splitlines() if "frame ms" not in line)
        code, _, err = quiet(text, 0.0)
        self.assertEqual(code, 1)
        self.assertIn("no heartbeat carried a frame-time window", err)

    def test_refuses_windows_that_are_all_warm_up(self) -> None:
        code, _, err = quiet(run_log(4), 60.0)
        self.assertEqual(code, 1)
        self.assertIn("none with frames after the 60s warm-up", err)


if __name__ == "__main__":
    unittest.main()
