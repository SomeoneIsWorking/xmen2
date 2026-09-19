#!/usr/bin/env python3
"""Tabulate the browser port's frame rate from its own heartbeat.

WHY THIS EXISTS. Every performance change to the browser target has to answer
"did the frame rate move", and the answer kept being read by eye out of a
console tail. That is wrong twice. WebLua's own `console` command returns a
rolling window of about fifty lines -- two and a half heartbeats -- so a reader
that polls slower than the window rolls silently drops the windows it did not
see and reports a median over whatever it happened to catch. And a single
median is the wrong summary on a shared machine: a run that another agent's
build walked over shows a collapse to two thirds of its rate at some arbitrary
elapsed time, and averaging that in buries a real change under someone else's
compile.

SO THIS REPORTS THE DISTRIBUTION AND THE PLATEAU. The plateau is the fastest
band the run sustained over at least `--plateau` consecutive heartbeats, which
is the machine's answer when nothing else was on it. Two builds are comparable
by their plateaus; they are not comparable by their medians unless the host was
quiet, and this prints the spread so that is visible rather than assumed.

THE NEGATIVE IS DESIGNED. A run that produced no heartbeat at all and a run
that produced heartbeats with no frames are different failures, and an empty
table cannot tell them apart, so neither prints one: this names which it was
and exits non-zero.

It reads the console through tools/web_console.py, which is the owner of that
boundary and the only thing here that talks to Chrome.
"""

from __future__ import annotations

import argparse
import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent

# `[HB]   50.1s  crossings 1256601 (+101926)` then, on the line after,
# `scenes 245 (+47)  clears 951 (+190)  draws 72124 (+13224)  presents 245 (+48)`.
# The elapsed time is on the first and the frame count is on the second, so
# neither line alone is a sample.
_ELAPSED = re.compile(r"\[HB\]\s+([0-9]+\.[0-9])s\s+crossings")
_PRESENTS = re.compile(r"presents (\d+) \(\+(\d+)\)")


def samples(text: str) -> list[tuple[float, int, int]]:
    """Every (elapsed, total, delta) the log carries, in heartbeat order."""
    found: list[tuple[float, int, int]] = []
    elapsed: float | None = None
    for line in text.splitlines():
        match = _ELAPSED.search(line)
        if match:
            elapsed = float(match.group(1))
            continue
        match = _PRESENTS.search(line)
        if match and elapsed is not None:
            found.append((elapsed, int(match.group(1)), int(match.group(2))))
            elapsed = None
    return found


def plateau(rates: list[float], window: int) -> tuple[float, float] | None:
    """The fastest band of `window` consecutive heartbeats, as (low, high)."""
    if len(rates) < window:
        return None
    best = max(range(len(rates) - window + 1),
               key=lambda start: min(rates[start:start + window]))
    band = rates[best:best + window]
    return min(band), max(band)


def report(text: str, interval: float, window: int) -> int:
    found = samples(text)
    if not found:
        print("web_presents: the log carries no heartbeat at all, so this "
              "measured nothing -- the route never started, or the console "
              "was not being recorded while it ran", file=sys.stderr)
        return 1
    print(f"{'elapsed':>8}  {'presents':>9}  {'delta':>6}  {'per second':>10}")
    for elapsed, total, delta in found:
        print(f"{elapsed:8.1f}  {total:9d}  {delta:6d}  {delta / interval:10.2f}")
    if found[-1][1] == 0:
        print(f"web_presents: {len(found)} heartbeat(s) and not one frame -- "
              "the guest is running and nothing is reaching the screen",
              file=sys.stderr)
        return 1
    rates = [delta / interval for _, _, delta in found]
    ordered = sorted(rates)
    print(f"windows {len(rates)}  min {ordered[0]:.2f}  "
          f"median {ordered[len(ordered) // 2]:.2f}  max {ordered[-1]:.2f}")
    band = plateau(rates, window)
    if band is None:
        print(f"web_presents: fewer than {window} heartbeats, so there is no "
              "plateau to report -- record for longer before comparing builds",
              file=sys.stderr)
        return 1
    print(f"plateau ({window} consecutive) {band[0]:.2f} - {band[1]:.2f} per second")
    print(steady(found, band, interval))
    return 0


def steady(found: list[tuple[float, int, int]], band: tuple[float, float],
           interval: float) -> str:
    """The rate across every heartbeat inside the plateau band, counted once.

    A per-heartbeat rate cannot resolve a small change. Over a five-second
    window one more frame is a whole step -- 57 to 58 presents is 1.75% -- so a
    build that genuinely freed two percent of the guest worker lands inside the
    SAME band as the build before it, and the plateau reads "unchanged" for a
    change that happened. Counting presents once across the whole plateau turns
    that step into the uncertainty of a single frame over minutes.

    Reported with that uncertainty rather than more decimal places than the
    measurement has, and with the span, so a short plateau cannot be read as a
    precise one.
    """
    runs: list[list[tuple[float, int, int]]] = [[]]
    for sample in found:
        if band[0] - 1e-9 <= sample[2] / interval <= band[1] + 1e-9:
            runs[-1].append(sample)
        elif runs[-1]:
            runs.append([])
    inside = max(runs, key=len)
    if len(inside) < 2:
        return ("steady rate: the plateau is a single heartbeat, which is a "
                "reading and not a rate -- record for longer")
    first, last = inside[0], inside[-1]
    span = last[0] - first[0]
    if span <= 0.0:
        return ("steady rate: the plateau is a single heartbeat, which is a "
                "reading and not a rate -- record for longer")
    frames = last[1] - first[1]
    rate = frames / span
    return (f"steady rate: {rate:.3f} +/- {1.0 / span:.3f} per second "
            f"({frames} presents over {span:.1f}s, {len(inside)} heartbeats; "
            f"the tolerance is one frame across the span)")


def record(profile: str, seconds: float, out: Path) -> str:
    subprocess.run(
        [sys.executable, str(ROOT / "tools/web_console.py"),
         "--profile", profile, "--seconds", str(seconds), "--out", str(out),
         "--grep", r"presents|crossings"],
        cwd=ROOT, check=True, stdout=subprocess.DEVNULL,
    )
    return out.read_text(errors="replace")


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument(
        "--profile", default="scratch/weblua-local/chrome-profile",
        help="the Chrome profile directory WebLua was given",
    )
    parser.add_argument("--seconds", type=float, default=300.0,
                        help="how long to record, when recording")
    parser.add_argument("--log", type=Path, default=None,
                        help="read this console log instead of recording one")
    parser.add_argument("--out", type=Path, default=ROOT / "scratch/web-profile/console.log",
                        help="where a recorded log is kept")
    parser.add_argument("--interval", type=float, default=5.0,
                        help="seconds between heartbeats, which the deltas are over")
    parser.add_argument("--plateau", type=int, default=6,
                        help="consecutive heartbeats a plateau must hold for")
    args = parser.parse_args(argv)

    if args.log:
        return report(args.log.read_text(errors="replace"), args.interval, args.plateau)
    args.out.parent.mkdir(parents=True, exist_ok=True)
    return report(record(args.profile, args.seconds, args.out),
                  args.interval, args.plateau)


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
