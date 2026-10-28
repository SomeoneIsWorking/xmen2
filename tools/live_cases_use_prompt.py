"""Live case for the use prompt as a touch button (docs/touch-play.md).

The run loads the Sanctuary hub, teleports the active hero with setPos* into
the trivia console's use trigger, taps the game's own "Use Trivia Game"
prompt near its edge and judges the result by GET /menu: the game's trivia
menu opens. See docs/RE/hud.md for the prompt.
"""

from __future__ import annotations

import json
import time

from live_cases_extraction import script
from live_game import live_controls, live_use_prompt, live_viewport
from live_harness import Case
from live_menu import keyboard_into_gameplay

HUB_MAP = "act1/sanctuary/sanctuary1"
# Inside trigger_use01 (2251.3, 2241.7, -356.6; 40 units each way in x and y).
TRIVIA = (2235.0, 2215.0, -356.0)
AWAY = (2235.0, 2015.0, -356.0)


def teleport(case: Case, at: tuple[float, float, float]) -> None:
    for _ in range(2):
        for axis, value in zip("XYZ", at, strict=True):
            script(case, "setPos%s('_ACTIVE_HERO_',%g)" % (axis, value))
    time.sleep(1.5)


def menu_class(case: Case) -> str:
    code, body = case.http("/menu")
    return json.loads(body).get("menu") or "" if code == 200 else ""


def press(case: Case, x: float, y: float) -> bool:
    """A finger held for a few frames, as a player taps."""
    down = case.http("/touch?x=%g&y=%g&phase=down" % (x, y))[0] == 200
    time.sleep(0.3)
    up = case.http("/touch?x=%g&y=%g&phase=up" % (x, y))[0] == 200
    return down and up


def wait_prompt(case: Case, timeout: float):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        rect = live_use_prompt(case)
        if rect:
            return rect
        time.sleep(0.3)
    return None


def wait_menu(case: Case, name: str, timeout: float) -> str:
    deadline = time.monotonic() + timeout
    shown = menu_class(case)
    while time.monotonic() < deadline and shown != name:
        time.sleep(0.3)
        shown = menu_class(case)
    return shown


def case_touch_use_prompt(case: Case) -> None:
    case.prepare_profile(["boot.mode=continue", "input.touch_controls=2"])
    case.seed_save("autosave0.save")
    case.launch({"X2_FILES": "1"})
    case.wait_control(60)
    menu = keyboard_into_gameplay(case, 300)
    case.check("the run reached gameplay through the PDA",
               menu.get("active") is False, str(menu.get("menu")))
    case.http("/console?command=loadmap%20" + HUB_MAP)
    time.sleep(5)
    menu = keyboard_into_gameplay(case, 300)
    case.check("loadmap %s reached gameplay" % HUB_MAP,
               menu.get("active") is False, str(menu.get("menu")))
    script(case, "setInvulnerable('_ACTIVE_HERO_','TRUE')")
    window = live_viewport(case)
    case.check("the overlay reports its viewport", window is not None)
    if not window:
        return

    teleport(case, AWAY)
    case.check("away from the console there is no use prompt button",
               live_use_prompt(case) is None)
    case.shot("away")

    teleport(case, TRIVIA)
    rect = wait_prompt(case, 10)
    case.check("in the trivia trigger the use prompt is a button",
               rect is not None)
    if not rect:
        return
    left, top, width, height = rect
    finger = 0.1 * min(window)
    case.check("the button is finger-sized (%gx%g, finger %g)"
               % (width, height, finger),
               width >= finger and height >= finger)
    case.check("the button sits in the bottom half, centred",
               top > window[1] * 0.5
               and abs(left + width * 0.5 - window[0] * 0.5) < window[0] * 0.1)
    case.shot("prompt")

    # Near the button's corner, outside the text the game draws.
    px, py = left + 4.0, top + 4.0
    under = [action for action, (cl, ct, cw, ch) in live_controls(case).items()
             if cl <= px < cl + cw and ct <= py < ct + ch]
    case.check("no overlay control lies under that point", not under,
               str(under))
    x, y = px / window[0], py / window[1]
    case.check("a finger near the button's edge is delivered",
               press(case, x, y))
    shown = wait_menu(case, "trivia", 10)
    case.check("the tap opened the trivia menu", shown == "trivia", shown)
    case.shot("trivia")
