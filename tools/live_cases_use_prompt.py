"""Live case for the use prompt as a touch button (docs/touch-play.md).

The run loads the Sanctuary hub, teleports the active hero with setPos* into
the trivia console's use trigger, taps the game's own "Use Trivia Game"
prompt near its edge and judges the result by GET /menu: the game's trivia
menu opens. Each variant runs in one window shape, and in each no drawn
control overlaps the prompt. The -xp variants award XP first, so the game's
XP notice shares the prompt's band, and tap the button while it does. See
docs/RE/hud.md for the prompt.
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
# The window each variant runs in: a tablet (a 2800x1840 panel's
# presentation), 4:3, 16:9 and a 20:9 phone.
SHAPES = {
    "touch-use-prompt": (1096, 720),
    "touch-use-prompt-4x3": (960, 720),
    "touch-use-prompt-16x9": (1280, 720),
    "touch-use-prompt-20x9": (1600, 720),
    "touch-use-prompt-xp-16x9": (1280, 720),
    "touch-use-prompt-xp-20x9": (1600, 720),
}


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


def sample_prompt(case: Case, seconds: float, shot: str) -> tuple[int, int]:
    """How often the prompt's button was published over `seconds`, and how
    often it was missing; the first sample is shot."""
    seen = lost = 0
    deadline = time.monotonic() + seconds
    while time.monotonic() < deadline:
        if live_use_prompt(case):
            seen += 1
        else:
            lost += 1
        if seen + lost == 1:
            case.shot(shot)
        time.sleep(0.05)
    return seen, lost


def wait_menu(case: Case, name: str, timeout: float) -> str:
    deadline = time.monotonic() + timeout
    shown = menu_class(case)
    while time.monotonic() < deadline and shown != name:
        time.sleep(0.3)
        shown = menu_class(case)
    return shown


def overlapping(rect, controls) -> list[str]:
    left, top, width, height = rect
    return [action for action, (cl, ct, cw, ch) in controls.items()
            if cl < left + width and left < cl + cw
            and ct < top + height and top < ct + ch]


def case_touch_use_prompt(case: Case) -> None:
    width, height = SHAPES[case.name]
    case.prepare_profile(["boot.mode=continue", "input.touch_controls=2",
                          "video.width=%d" % width,
                          "video.height=%d" % height,
                          "video.mode=windowed"])
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
    case.check("the button sits in the bottom half, near the centre",
               top > window[1] * 0.5
               and abs(left + width * 0.5 - window[0] * 0.5) < window[0] * 0.1)
    controls = live_controls(case)
    (case.dir / "controls.txt").write_text(case.get_text("/controls"))
    case.check("the gameplay controls are drawn, Jump among them",
               "jump" in controls, str(sorted(controls)))
    covered = overlapping(rect, controls)
    case.check("in %gx%g no drawn control overlaps the button %s"
               % (window[0], window[1], rect), not covered,
               "; ".join("%s %s" % (name, controls[name]) for name in covered))
    case.shot("prompt")
    if "-xp" in case.name:
        # The notice stays up about two seconds; the button is read through
        # its first second and pressed inside it.
        script(case, "awardXPToPlayable(500)")
        seen, lost = sample_prompt(case, 1.0, "prompt-xp")
        case.check("with the XP notice in its band the prompt stayed a "
                   "button (%d sample(s), %d without it)" % (seen, lost),
                   seen >= 5 and lost == 0)
        rect = live_use_prompt(case)
        if not rect:
            return
        left, top, width, height = rect
        covered = overlapping(rect, live_controls(case))
        case.check("with the XP notice no drawn control overlaps the "
                   "button %s" % (rect,), not covered, str(covered))

    # Near the button's corner, outside the text the game draws.
    px, py = left + 4.0, top + 4.0
    under = overlapping((px, py, 1.0, 1.0), controls)
    case.check("no overlay control lies under that point", not under,
               str(under))
    x, y = px / window[0], py / window[1]
    case.check("a finger near the button's edge is delivered",
               press(case, x, y))
    shown = wait_menu(case, "trivia", 10)
    case.check("the tap opened the trivia menu", shown == "trivia", shown)
    case.shot("trivia")
