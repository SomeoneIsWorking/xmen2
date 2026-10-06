"""Helpers that read and drive the running game for live cases: scenes,
pad polling, the touch overlay's controls, and frame comparison."""

from __future__ import annotations

import re
import time
from pathlib import Path

from live_harness import Case


def reach_camera_only_lock(case: Case, timeout: float) -> str:
    """Wait for the authored sequence's CAMERA-ONLY stretch: the control lock
    holds and no conversation record is visible. That is the opening pan the
    tutorial plays for seconds before its first line exists -- the stretch a
    player is most likely to press Escape in, and the one the visible-line
    path never covers. Returns the report that proved it, or ""."""
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        if not case.alive():
            return ""
        report = case.get_text("/input?controller=0")
        if "Cutscene player: active 1" in report \
                and "boundary: controls locked; conversation payload inactive" \
                in report:
            return report
        time.sleep(0.5)
    return ""


def reach_authored_conversation(case: Case, timeout: float) -> bool:
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        if not case.alive():
            return False
        report = case.get_text("/input?controller=0")
        if "Cutscene player: active 1" in report \
                and "conversation payload deterministic" in report:
            return True
        time.sleep(1.0)
    return False


def pad_poll_rate(case: Case) -> int:
    """The latest 'the game read a button N time(s)' heartbeat counter."""
    n = 0
    for line in case.log_text().splitlines():
        if "the game read a button" in line:
            digits = [int(t) for t in line.replace(",", " ").split()
                      if t.isdigit()]
            if digits:
                n = digits[0]
    return n


def wait_pad_polled(case: Case, timeout: float) -> bool:
    """Wait until the GAME (not the probe) is polling the pad: the heartbeat
    counter must grow well beyond what this harness's own /input polls add."""
    start = pad_poll_rate(case)
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        if not case.alive():
            return False
        if pad_poll_rate(case) > start + 200:
            return True
        time.sleep(1.0)
    return False


def wait_controls_unlocked(case: Case, timeout: float) -> bool:
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        if not case.alive():
            return False
        report = case.get_text("/input?controller=0")
        if "boundary: controls released; conversation payload inactive" \
                in report:
            return True
        time.sleep(1.0)
    return False


def png_mean_diff(a: Path, b: Path,
                  box: tuple[float, float, float, float] | None = None) -> float:
    """Mean grey-level difference, over the whole frame or over `box`
    (left, top, right, bottom as fractions of the frame)."""
    from PIL import Image  # the locked uv environment owns Pillow
    try:
        ia, ib = Image.open(a).convert("L"), Image.open(b).convert("L")
    except Exception as exc:
        print("  [WARN] screenshot comparison unavailable: %s" % exc)
        return 0.0
    if ia.size != ib.size:
        ia = ia.resize(ib.size)
    if box:
        crop = tuple(int(f * ib.size[i % 2]) for i, f in enumerate(box))
        ia, ib = ia.crop(crop), ib.crop(crop)
    pa, pb = list(ia.getdata()), list(ib.getdata())
    n = min(len(pa), len(pb))
    if not n:
        return 0.0
    step = max(1, n // 4096)
    count = len(range(0, n, step))
    return sum(abs(pa[i] - pb[i]) for i in range(0, n, step)) / count


def live_viewport(case: Case, route: str = "/controls") -> tuple[float, float] | None:
    """The surface the run published these rectangles in.

    Whichever route is asked reports its own, so a caller never divides a
    rectangle by a window size it learned somewhere else -- the mistake that
    once sent a tap off the bottom of the window.
    """
    for line in case.get_text(route).splitlines():
        found = re.match(r"viewport ([\d.]+)x([\d.]+)", line)
        if found:
            return (float(found.group(1)), float(found.group(2)))
    return None


def live_controls(case: Case) -> dict[str, tuple[float, float, float, float]]:
    """What the overlay draws right now, by action: left, top, width, height.

    Read from the run rather than decided before it: a coordinate chosen in
    advance answers whatever screen the run drifted onto, and two earlier
    measurements were read as evidence that way.
    """
    out = {}
    for line in case.get_text("/controls").splitlines():
        parts = line.split()
        if len(parts) < 5 or "," not in parts[2] or "x" not in parts[3]:
            continue
        left, top = (float(v) for v in parts[2].split(","))
        width, height = (float(v) for v in parts[3].split("x"))
        out[parts[4]] = (left, top, width, height)
    return out


def tap_control(case: Case, rect, window: tuple[float, float]) -> bool:
    """Tap the centre of a drawn control, in fractions of ITS surface."""
    left, top, width, height = rect
    return case.http("/touch?x=%g&y=%g" % ((left + width * 0.5) / window[0],
                                           (top + height * 0.5) / window[1])
                     )[0] == 200


def live_stick(case: Case) -> tuple[tuple[float, float, float, float],
                                    tuple[float, float]] | None:
    """The movement ring the overlay is drawing, and its live deflection.

    Read from the run, not recomputed here: a case that works out where the
    ring ought to be keeps a second copy of the layout and stops testing the
    control the player actually touches.
    """
    for line in case.get_text("/controls").splitlines():
        parts = line.split()
        if len(parts) < 7 or parts[1] != "stick" or parts[5] != "deflect":
            continue
        left, top = (float(v) for v in parts[2].split(","))
        width, height = (float(v) for v in parts[3].split("x"))
        dx, dy = (float(v) for v in parts[6].split(","))
        return ((left, top, width, height), (dx, dy))
    return None
