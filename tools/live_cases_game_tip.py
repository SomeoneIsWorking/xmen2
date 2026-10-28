"""Live case for a Game Tip on touch (docs/touch-play.md).

The tutorial's Game Tips are CPopupDialog messages a "?" trigger opens in
gameplay, with no menu under them. The run continues the tutorial autosave,
teleports the active hero into trigger_hint05 near the start, taps its "Game
Tip" use prompt, and judges the switching hint the game opens by GET /menu:
its message wrapped inside the touch frame and above a finger-sized Continue
button, no menu pad drawn over it, and a tap on Continue closing it.
"""

from __future__ import annotations

import json
import time

from live_cases_extraction import script
from live_cases_use_prompt import press, teleport, wait_prompt
from live_game import live_controls, live_viewport
from live_harness import Case
from live_menu import finger_tall, keyboard_into_gameplay, read_menu

# The switching hint's trigger and the words of its message.
TRIGGER = "trigger_hint05"
MESSAGE = "teammates"


def trigger_at(case: Case) -> tuple[float, float, float] | None:
    code, body = case.http("/entities?match=" + TRIGGER)
    entities = json.loads(body).get("entities", []) if code == 200 else []
    if not entities:
        return None
    return entities[0]["x"], entities[0]["y"], entities[0]["z"]


def wait_popup(case: Case, up: bool, timeout: float) -> dict:
    deadline = time.monotonic() + timeout
    menu = read_menu(case)
    while time.monotonic() < deadline and bool(menu.get("popup")) != up:
        time.sleep(0.3)
        menu = read_menu(case)
    return menu


def inside(low: dict, high: dict, outer_low: dict, outer_high: dict) -> bool:
    """A window-fraction box inside another; y grows down the window."""
    return (outer_low["x"] <= low["x"] and high["x"] <= outer_high["x"]
            and outer_high["y"] <= high["y"] and low["y"] <= outer_low["y"])


def case_touch_game_tip(case: Case) -> None:
    width, height = (1280, 720) if case.name.endswith("720") else (1096, 720)
    case.prepare_profile(["boot.mode=continue", "input.touch_controls=2",
                          "video.width=%d" % width, "video.height=%d" % height,
                          "video.mode=windowed"])
    case.seed_save("autosave0.save")
    case.launch({"X2_FILES": "1"})
    case.wait_control(60)
    menu = keyboard_into_gameplay(case, 300)
    case.check("the run reached gameplay through the PDA",
               menu.get("active") is False, str(menu.get("menu")))
    script(case, "setInvulnerable('_ACTIVE_HERO_','TRUE')")
    window = live_viewport(case)
    case.check("the window is %dx%d" % (width, height),
               window is not None and tuple(window) == (width, height),
               str(window))
    at = trigger_at(case)
    case.check("the tutorial has %s" % TRIGGER, at is not None)
    if not window or not at:
        return
    case.check("no popup is up before the tip",
               read_menu(case).get("popup") is False)

    teleport(case, (at[0], at[1] - 30.0, at[2]))
    rect = wait_prompt(case, 10)
    case.check("in the trigger the Game Tip prompt is a button",
               rect is not None)
    if not rect:
        return
    left, top, _, _ = rect
    case.check("a tap on the prompt is delivered",
               press(case, (left + 4.0) / window[0], (top + 4.0) / window[1]))
    menu = wait_popup(case, True, 10)
    page = menu.get("popup_page") or {}
    case.check("the tap opened the switching hint",
               MESSAGE in page.get("text", "").lower(),
               page.get("text", "")[:60])
    time.sleep(1.0)
    menu = read_menu(case)
    page = menu.get("popup_page") or {}
    (case.dir / "game-tip.json").write_text(json.dumps(menu, indent=1) + "\n")
    controls = live_controls(case)
    case.check("the menu pad is not drawn over the tip",
               not any(name.startswith("menu-") for name in controls),
               "drawn: %s" % sorted(controls))
    case.check("the tip has its touch layout", page.get("touch") is True)
    frame = (page.get("frame_low"), page.get("frame_high"))
    text = (page.get("text_low"), page.get("text_high"))
    footers = page.get("footers", [])
    case.check("the frame stays in the window",
               None not in frame
               and inside(*frame, {"x": 0.0, "y": 1.0}, {"x": 1.0, "y": 0.0}),
               str(frame))
    case.check("the message is wrapped inside the frame",
               None not in frame and None not in text
               and inside(*text, *frame), str(text))
    case.check("one footer, Continue, a finger tall",
               len(footers) == 1 and footers[0].get("text") == "Continue"
               and finger_tall(footers[0]), str(footers))
    if len(footers) != 1 or None in text:
        case.shot("game-tip")
        return
    button = footers[0]
    case.check("Continue sits below the message",
               button["edge"]["y"] > text[0]["y"],
               "button %.3f, text bottom %.3f"
               % (button["edge"]["y"], text[0]["y"]))
    case.shot("game-tip")

    case.check("a tap at Continue's edge is delivered",
               press(case, button["edge"]["x"], button["edge"]["y"]))
    menu = wait_popup(case, False, 10)
    case.check("the tap closed the tip", menu.get("popup") is False,
               json.dumps(menu)[:120])
    time.sleep(1.0)
    controls = live_controls(case)
    case.check("gameplay's controls are back",
               any(not name.startswith("menu-") for name in controls)
               and not any(name.startswith("menu-") for name in controls),
               "drawn: %s" % sorted(controls))
    case.shot("after")
