#!/usr/bin/env python3
"""Ask a mobile browser what a finger on the game canvas actually does.

The page owns whether a drag over the canvas reaches the game or is eaten by
the browser's own scroll, zoom, selection and long-press gestures. This drives
a real touch device through CDP and reports each of those separately, so a
"touch works" claim cannot be made from a screenshot of the overlay.

It is built to print the OTHER answer: every observation below is reported with
the value it actually read, and a run that never reached the canvas refuses
rather than reporting a tidy row of "no gesture".
"""

from __future__ import annotations

import argparse
import json
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from cdp_client import Cdp, targets


def page_socket(port: int) -> Cdp:
    for _ in range(40):
        for target in targets(port):
            if target.get("type") == "page" and "webSocketDebuggerUrl" in target:
                return Cdp(target["webSocketDebuggerUrl"])
        time.sleep(0.5)
    raise SystemExit(f"no page target on CDP port {port}")


def evaluate(cdp: Cdp, expression: str):
    result = cdp.call(
        "Runtime.evaluate",
        {"expression": expression, "returnByValue": True, "awaitPromise": True},
    )
    if "exceptionDetails" in result:
        raise SystemExit(f"page refused the probe: {result['exceptionDetails']}")
    return result["result"].get("value")


def touch(cdp: Cdp, kind: str, points: list[dict]) -> None:
    cdp.call("Input.dispatchTouchEvent", {"type": kind, "touchPoints": points})


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", type=int, required=True, help="CDP port of a running browser")
    parser.add_argument("--width", type=int, default=390)
    parser.add_argument("--height", type=int, default=844)
    args = parser.parse_args()

    cdp = page_socket(args.port)
    cdp.call("Runtime.enable")
    cdp.call("Page.enable")
    cdp.call(
        "Emulation.setDeviceMetricsOverride",
        {
            "width": args.width,
            "height": args.height,
            "deviceScaleFactor": 3,
            "mobile": True,
            "screenOrientation": {"type": "portraitPrimary", "angle": 0},
        },
    )
    cdp.call("Emulation.setTouchEmulationEnabled", {"enabled": True, "maxTouchPoints": 5})

    # The canvas is the surface a finger must reach. Show it the way a playing
    # page does rather than inventing a second element to test.
    shown = evaluate(
        cdp,
        """(() => {
          document.body.classList.add('playing');
          const canvas = document.querySelector('#canvas');
          if (!canvas) return null;
          const box = canvas.getBoundingClientRect();
          return {w: box.width, h: box.height, x: box.x, y: box.y};
        })()""",
    )
    if not shown or shown["w"] < 1 or shown["h"] < 1:
        raise SystemExit(f"the canvas never became a touchable surface: {shown!r}")

    # Make the document taller than the viewport, so a browser scroll that is
    # not suppressed has somewhere to go. A page that cannot scroll would
    # report "no scroll" whatever the canvas does -- that is the false pass
    # this guards against.
    evaluate(
        cdp,
        """(() => {
          let pad = document.querySelector('#probe-pad');
          if (!pad) {
            pad = document.createElement('div');
            pad.id = 'probe-pad';
            pad.style.height = '400vh';
            document.body.appendChild(pad);
          }
          window.scrollTo(0, 0);
          window.__probe = {prevented: 0, touchstarts: 0, contextmenu: 0};
          addEventListener('touchstart', e => {
            window.__probe.touchstarts++;
            if (e.defaultPrevented || e.cancelable === false) window.__probe.prevented++;
          }, {passive: true});
          addEventListener('contextmenu', () => window.__probe.contextmenu++, {passive: true});
          return true;
        })()""",
    )

    style = evaluate(
        cdp,
        """(() => {
          const canvas = document.querySelector('#canvas');
          const s = getComputedStyle(canvas);
          return {
            touchAction: s.touchAction,
            userSelect: s.userSelect || s.webkitUserSelect,
            touchCallout: s.webkitTouchCallout || '(unsupported)',
            overscroll: getComputedStyle(document.documentElement).overscrollBehavior,
            canvasHeightPx: canvas.getBoundingClientRect().height,
            innerHeight: window.innerHeight,
            visualHeight: visualViewport ? visualViewport.height : null,
          };
        })()""",
    )

    mid_x = shown["x"] + shown["w"] / 2
    start_y = shown["y"] + shown["h"] * 0.7
    for index in range(12):
        y = start_y - index * (shown["h"] * 0.04)
        point = [{"x": mid_x, "y": y, "id": 1, "radiusX": 12, "radiusY": 12, "force": 1}]
        touch(cdp, "touchStart" if index == 0 else "touchMove", point)
        time.sleep(0.02)
    touch(cdp, "touchEnd", [])
    time.sleep(0.4)

    after = evaluate(
        cdp,
        """({
          scrollY: window.scrollY,
          scale: visualViewport ? visualViewport.scale : null,
          selection: String(getSelection()),
          probe: window.__probe,
        })""",
    )

    report = {"viewport": {"width": args.width, "height": args.height}, "style": style, "afterDrag": after}
    print(json.dumps(report, indent=2))

    verdicts = []
    verdicts.append(("the page scrolled under the finger", after["scrollY"] > 0, f"scrollY={after['scrollY']}"))
    verdicts.append(
        ("the canvas lets the browser own the gesture", style["touchAction"] in ("auto", ""),
         f"touch-action={style['touchAction']!r}")
    )
    verdicts.append(
        ("a drag can select page text", style["userSelect"] not in ("none",),
         f"user-select={style['userSelect']!r}")
    )
    verdicts.append(
        ("the canvas is sized by innerHeight, not the visual viewport",
         style["visualHeight"] is not None and abs(style["canvasHeightPx"] - style["visualHeight"]) > 1.0,
         f"canvas={style['canvasHeightPx']} visual={style['visualHeight']}")
    )
    if after["probe"]["touchstarts"] == 0:
        raise SystemExit("no touchstart reached the document: the probe never touched the canvas")
    print(f"\ntouchstart events seen: {after['probe']['touchstarts']}")
    for label, failed, detail in verdicts:
        print(f"{'DEFECT ' if failed else 'ok     '} {label} ({detail})")
    return 1 if any(failed for _, failed, _ in verdicts) else 0


if __name__ == "__main__":
    raise SystemExit(main())
