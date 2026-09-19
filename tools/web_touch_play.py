#!/usr/bin/env python3
"""Tap the game's own on-screen controls in a browser and read what the pad got.

The overlay drawing and a zone lighting up prove nothing about gameplay: the
chain that matters is contact -> zone -> virtual pad -> player one -> guest.
Only the port's touch census can see the far end of that, so this tool does
the one thing the census cannot: it produces real contacts on a real browser's
touch device, over CDP, on the running product.

It does NOT recompute the control layout. src/presentation/touch_layout.h is
the single authority for where the controls are, and a second copy here could
only agree with it by luck -- that is the exact bug that header was written to
end. Instead this sweeps a grid over the whole canvas and lets the census say
what was reached. A grid that hits no control and a chain that drops every
contact give different census lines, which is the whole point.

The census is read from the periodic heartbeat, because a browser tab never
exits and so never reaches the end-of-run roll-call.

Refusals, not tidy zeroes: it stops and says which stage was not reached if
the page has no canvas, if the run never beat, or if no contact arrived.
"""

from __future__ import annotations

import argparse
import json
import re
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from cdp_client import Cdp, CdpError, browser_endpoint, evaluate, page_socket, touch
from cdp_console import CONSOLE_EVENTS, attach_all_targets, console_line

CANVAS_RECT = """(() => {
  const c = document.querySelector("canvas");
  if (!c) return null;
  const r = c.getBoundingClientRect();
  return {x: r.x, y: r.y, width: r.width, height: r.height,
          hidden: getComputedStyle(c).display === "none"};
})()"""


class Census:
    """The product's own touch account, read from its heartbeat.

    The census is the only thing that can see the far end of the chain, so
    this parses its lines rather than guessing from the page. The
    heartbeat's "[HB] " tag is optional here because the same text is printed
    by the end-of-run roll-call, and tests/test_web_touch_play.py reads the
    real thing from the C test's own report -- a reader checked only against
    hand-typed fixtures drifts from the code that prints them. Every field
    starts at None -- "the heartbeat has not said" and "the heartbeat said
    zero" are different answers and must not collapse into one.
    """

    DROPPED = re.compile(
        r"\[touch\] (?:\[HB\] )?(\d+) of (\d+) dropped before routing: "
        r"(\d+) with no window, (\d+) with the overlay hidden "
        r"\(touch_controls=(\S+), source says ([^,]+), gate ([^)]+)\)"
    )
    CONTACTS = re.compile(r"\[touch\] (?:\[HB\] )?(\d+) contact event\(s\)")
    NOTHING = re.compile(
        r"\[touch\] (?:\[HB\] )?no contact reached the port this run -- "
        r"touch_controls=(\S+), source says ([^,]+), gate ([^,]+),"
    )
    ZONES = re.compile(r"\[touch\] (?:\[HB\] )?(\d+) zone action\(s\) routed")
    PUBLISHED = re.compile(
        r"\[touch\] (?:\[HB\] )?published to the pad: (\d+) button change\(s\) "
        r"\((\d+) refused\), (\d+) axis change\(s\) \((\d+) refused\)"
    )
    PLAYER_ONE = re.compile(r"\[touch\] (?:\[HB\] )?(the touch pad was \w+|player one already had)")

    def __init__(self) -> None:
        self.beats = 0
        self.contacts = None
        self.dropped = None
        self.gate = None
        self.source = None
        self.mode = None
        self.zones = None
        self.buttons = None
        self.buttons_refused = None
        self.axes = None
        self.axes_refused = None
        self.player_one = None

    def feed(self, line: str) -> None:
        match = self.NOTHING.search(line)
        if match:
            self.beats += 1
            self.contacts, self.dropped = 0, 0
            self.mode, self.source, self.gate = match.group(1, 2, 3)
            return
        match = self.CONTACTS.search(line)
        if match:
            self.beats += 1
            self.contacts = int(match.group(1))
            return
        match = self.DROPPED.search(line)
        if match:
            self.dropped = int(match.group(1))
            self.mode, self.source, self.gate = match.group(5, 6, 7)
            return
        match = self.ZONES.search(line)
        if match:
            self.zones = int(match.group(1))
            return
        match = self.PUBLISHED.search(line)
        if match:
            (self.buttons, self.buttons_refused,
             self.axes, self.axes_refused) = (int(g) for g in match.group(1, 2, 3, 4))
            return
        match = self.PLAYER_ONE.search(line)
        if match:
            self.player_one = match.group(1)


def pump(cdp: Cdp, census: Census, seconds: float) -> None:
    """Read the console for `seconds`, feeding every line to the census."""
    cdp.drain(seconds)
    while cdp.events:
        event = cdp.events.pop(0)
        if event.get("method") not in CONSOLE_EVENTS:
            continue
        line = console_line(event)
        if line:
            census.feed(line)


def press(page: Cdp, key: str, code: str, windows_key: int) -> None:
    """One key down/up at the page, the way a keyboard reaches the canvas."""
    for kind in ("rawKeyDown", "keyUp"):
        page.call(
            "Input.dispatchKeyEvent",
            {"type": kind, "key": key, "code": code,
             "windowsVirtualKeyCode": windows_key,
             "nativeVirtualKeyCode": windows_key},
        )


def skip_a_cutscene(page: Cdp) -> None:
    """Ask the port to skip an authored conversation or cutscene.

    The tutorial map opens on a scripted conversation, and its HUD -- which is
    the gate this tool waits on -- does not draw until that conversation ends.
    Escape and Start are the port's own cancellation route for one. This is
    the same thing a player does, not a fast-forward: the port owns what the
    skip transitions to.
    """
    press(page, "Escape", "Escape", 27)
    press(page, "Enter", "Enter", 13)


def wait_for_gameplay(cdp: Cdp, census: Census, deadline: float,
                      page: Cdp | None = None) -> bool:
    """Until the product's own gate says controls belong on screen.

    Tapping before this is a measurement of nothing: the overlay is correctly
    hidden during the logo and the loading route, so every contact is
    correctly dropped and the run proves only that the tapper was early.
    """
    while time.monotonic() < deadline:
        pump(cdp, census, 5.0)
        if census.gate == "active":
            return True
        if page is not None and census.gate in (None, "never-seen", "hud-stale",
                                                "cutscene-locked"):
            skip_a_cutscene(page)
        print(f"  gate {census.gate or 'not yet reported'} "
              f"after {census.beats} beat(s); waiting")
    return False


def canvas_rect(cdp, deadline: float) -> dict:
    """The drawn canvas, once the product is actually showing one."""
    last = None
    while time.monotonic() < deadline:
        last = evaluate(cdp, CANVAS_RECT)
        if last and not last["hidden"] and last["width"] > 0:
            return last
        time.sleep(1.0)
    raise CdpError(
        "no drawn canvas before the deadline; the page reported "
        f"{json.dumps(last)}. Nothing was tapped."
    )


def tap(cdp, x: float, y: float, hold: float) -> None:
    point = {"x": x, "y": y, "radiusX": 12, "radiusY": 12, "force": 1.0, "id": 1}
    touch(cdp, "touchStart", [point])
    time.sleep(hold)
    touch(cdp, "touchEnd", [])


def sweep(cdp, rect: dict, columns: int, rows: int, hold: float) -> list[tuple[float, float]]:
    """A grid of taps over the canvas, bottom rows first.

    The controls live along the bottom edge, so the bottom row is tapped
    first: a run cut short still tapped where the controls are.
    """
    points = []
    for row in reversed(range(rows)):
        for column in range(columns):
            x = rect["x"] + rect["width"] * (column + 0.5) / columns
            y = rect["y"] + rect["height"] * (row + 0.5) / rows
            points.append((x, y))
    for x, y in points:
        tap(cdp, x, y, hold)
        time.sleep(0.08)
    return points


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", type=int, required=True, help="CDP port of a running browser")
    parser.add_argument("--width", type=int, default=390, help="emulated device width")
    parser.add_argument("--height", type=int, default=844, help="emulated device height")
    parser.add_argument("--columns", type=int, default=6)
    parser.add_argument("--rows", type=int, default=4)
    parser.add_argument("--hold", type=float, default=0.35, help="seconds a tap is held")
    parser.add_argument(
        "--canvas-timeout", type=float, default=180.0,
        help="seconds to wait for the product to draw a canvas",
    )
    parser.add_argument(
        "--gameplay-timeout", type=float, default=420.0,
        help="seconds to wait for the product's gate to reach gameplay",
    )
    parser.add_argument(
        "--no-skip", action="store_true",
        help="do not send Escape/Enter while waiting; measure the route as it is",
    )
    parser.add_argument(
        "--settle", type=float, default=16.0,
        help="seconds to wait after the sweep so at least one heartbeat prints",
    )
    args = parser.parse_args()

    census = Census()
    console = Cdp(browser_endpoint(args.port))
    sessions = attach_all_targets(console)
    print(f"attached to {len(sessions)} target(s)")

    cdp = page_socket(args.port)
    cdp.call("Runtime.enable")
    cdp.call("Page.enable")
    cdp.call(
        "Emulation.setDeviceMetricsOverride",
        {"width": args.width, "height": args.height,
         "deviceScaleFactor": 3, "mobile": True},
    )
    cdp.call("Emulation.setTouchEmulationEnabled", {"enabled": True, "maxTouchPoints": 5})
    cdp.call("Emulation.setEmitTouchEventsForMouse", {"enabled": False})

    rect = canvas_rect(cdp, time.monotonic() + args.canvas_timeout)
    print(f"canvas {rect['width']:.0f}x{rect['height']:.0f} at "
          f"({rect['x']:.0f}, {rect['y']:.0f})")

    print("waiting for the product's gate to reach gameplay")
    reached = wait_for_gameplay(
        console, census, time.monotonic() + args.gameplay_timeout,
        page=None if args.no_skip else cdp,
    )
    if not reached:
        print(f"REFUSED: the gate never reached 'active' -- it was last "
              f"{census.gate or 'never reported'} after {census.beats} beat(s). "
              f"The controls are correctly hidden outside gameplay, so tapping "
              f"now would measure the tapper, not the feature. Nothing was tapped.")
        return 2
    print(f"gate active after {census.beats} beat(s)")

    before = (census.contacts or 0, census.zones or 0, census.buttons or 0, census.axes or 0)
    points = sweep(cdp, rect, args.columns, args.rows, args.hold)
    print(f"dispatched {len(points)} tap(s) over a {args.columns}x{args.rows} grid, "
          f"{args.hold:.2f}s each")

    # A tap the browser kept would leave the page scrolled. The canvas claim
    # is supposed to make that impossible, so it is checked rather than
    # assumed: a scrolled page means the finger never reached the game.
    scrolled = evaluate(cdp, "window.scrollY")
    print(f"page scrollY after the sweep: {scrolled} "
          f"({'the browser ate at least one gesture' if scrolled else 'the canvas kept every gesture'})")

    pump(console, census, args.settle)
    after = (census.contacts or 0, census.zones or 0, census.buttons or 0, census.axes or 0)
    print("\n--- what the product says the sweep did ---")
    print(f"  mode {census.mode}, source {census.source}, gate {census.gate}")
    for name, was, now in zip(
        ("contact events", "zone actions", "pad buttons", "pad axes"), before, after, strict=True
    ):
        print(f"  {name:<14} {was} -> {now}  (+{now - was})")
    print(f"  refused by the pad: {census.buttons_refused} button(s), "
          f"{census.axes_refused} axis change(s)")
    print(f"  dropped before routing, cumulative: {census.dropped} of {census.contacts}")
    print(f"  player one: {census.player_one}")

    contacts, zones, buttons, axes = (now - was for was, now in zip(before, after, strict=True))
    if contacts == 0:
        print("\nVERDICT: no contact reached the port. The taps did not become "
              "SDL finger events at all -- this is the browser or the canvas, "
              "not the touch chain.")
        return 1
    if zones == 0:
        print(f"\nVERDICT: {contacts} contact(s) reached the port and none landed "
              "on a control. Either the grid missed every zone or the layout "
              "put them somewhere the sweep did not cover.")
        return 1
    if buttons == 0 and axes == 0:
        print(f"\nVERDICT: {zones} zone action(s) routed and NOTHING reached the "
              "pad. This is the failure that matters: the overlay is live and "
              "the guest cannot see it.")
        return 1
    print(f"\nVERDICT: a touch on a drawn control reaches the pad in this browser "
          f"-- {contacts} contact(s), {zones} zone action(s), {buttons} button "
          f"change(s) and {axes} axis change(s) published.")
    if census.player_one and census.player_one.startswith("player one already"):
        print("  NOTE: player one already had a controller, so the touch pad was "
              "not claimed for it. Published presses can still be going to a pad "
              "the guest is not reading.")
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
