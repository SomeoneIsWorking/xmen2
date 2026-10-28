"""Live case for the automap on touch (docs/touch-play.md).

The run boots into act2/jungle/jungle1, taps through the level's opening
line, and opens CMenuAutoMap with the console's `openmenu automap`. It judges
by GET /menu's `automap` (where the map looks, its turn and bounds, read from
the menu) that a drag moves the map with the finger, that a finger held
still does not pan it, that each Rotate button turns it its own way while
held, and that a tap on Back's edge outside the game's own box closes it. No
menu pad is drawn over it.
"""

from __future__ import annotations

import math
import time
import urllib.parse

from live_game import live_controls
from live_harness import Case
from live_menu import finger_tall, read_menu

SHAPES = {
    "touch-automap": (1280, 720),
    "touch-automap-4x3": (960, 720),
    "touch-automap-tablet": (1216, 800),
    "touch-automap-20x9": (1600, 720),
}
LEVEL = "act2/jungle/jungle1"
# Scene coordinates reach the menu as integers (FUN_005f9eb0), a world unit
# being the map scale's inverse of one: a few units of slack.
WORLD_SLACK = 15.0


def into_level(case: Case, width: int, height: int) -> bool:
    """Booted into the level in a width x height window and past its opening
    line, gameplay's controls drawn."""
    case.prepare_profile(["boot.mode=menu", "input.touch_controls=2",
                          "video.width=%d" % width,
                          "video.height=%d" % height,
                          "video.mode=windowed"])
    case.launch({"X2_BOOT_MAP": LEVEL})
    case.wait_control(120)
    deadline = time.monotonic() + 240
    calm = 0
    while time.monotonic() < deadline:
        menu = read_menu(case)
        shown = menu.get("conversation")
        if shown and shown.get("accepts") and shown.get("responses"):
            tap(case, shown["responses"][0]["tap"])
            calm = 0
        elif not shown and any(not name.startswith("menu-")
                               for name in live_controls(case)):
            calm += 1
            if calm >= 4:
                return True
        time.sleep(0.5)
    return False


def tap(case: Case, at: dict, hold: float = 0.3) -> bool:
    down = case.http("/touch?x=%g&y=%g&phase=down" % (at["x"], at["y"]))
    time.sleep(hold)
    up = case.http("/touch?x=%g&y=%g&phase=up" % (at["x"], at["y"]))
    return down[0] == 200 and up[0] == 200


def drag(case: Case, start: dict, dx: float, dy: float) -> None:
    """One finger from `start` by (dx, dy) window fractions, in steps."""
    steps = 5
    case.http("/touch?x=%g&y=%g&phase=down" % (start["x"], start["y"]))
    for i in range(1, steps + 1):
        time.sleep(0.1)
        case.http("/touch?x=%g&y=%g&phase=motion"
                  % (start["x"] + dx * i / steps, start["y"] + dy * i / steps))
    time.sleep(0.3)
    case.http("/touch?x=%g&y=%g&phase=up"
              % (start["x"] + dx, start["y"] + dy))
    time.sleep(0.5)


def automap(case: Case) -> dict:
    return read_menu(case).get("automap") or {}


def buttons(menu: dict) -> dict[str, dict]:
    """The automap's finger buttons by name; Back is the click button."""
    out = {}
    for button in menu.get("touch_buttons", []):
        out[button.get("name") or ("back" if button.get("press") == "click"
                                   else "?")] = button
    return out


def scene_per_window(button: dict) -> tuple[float, float]:
    """Scene units per window fraction across and down, from a button's
    centre and its edge point (a tenth across, nine tenths up; the window's
    y grows down)."""
    area = button["area"]
    width = area["right"] - area["left"]
    height = area["top"] - area["bottom"]
    return (0.4 * width / (button["tap"]["x"] - button["edge"]["x"]),
            0.4 * height / (button["tap"]["y"] - button["edge"]["y"]))


def expected_after(view: dict, moved_x: float, moved_z: float) -> tuple:
    """Where the map looks after a finger moved (moved_x, moved_z) scene
    units: the ground under the finger stays under it (the markers' draw,
    FUN_005a0bb0, takes the ground to the screen by the turn's inverse at
    the map scale), held in the bounds."""
    c, s = math.cos(view["turn"]), math.sin(view["turn"])
    dx = (moved_x * c - moved_z * s) / view["scale"]
    dy = (moved_x * s + moved_z * c) / view["scale"]
    x = min(max(view["x"] - dx, view["low_x"]), view["high_x"])
    y = min(max(view["y"] - dy, view["low_y"]), view["high_y"])
    return x, y


def inside_window(button: dict) -> bool:
    return all(0.0 <= button[key][axis] <= 1.0
               for key in ("tap", "edge") for axis in ("x", "y"))


def disjoint(a: dict, b: dict) -> bool:
    return (a["right"] <= b["left"] or b["right"] <= a["left"]
            or a["top"] <= b["bottom"] or b["top"] <= a["bottom"])


def check_drag(case: Case, what: str, scale: tuple[float, float],
               dx: float, dy: float) -> None:
    before = automap(case)
    start = {"x": 0.5, "y": 0.4}
    drag(case, start, dx, dy)
    after = automap(case)
    want = expected_after(before, dx * scale[0], -dy * scale[1])
    moved = math.hypot(after["x"] - before["x"], after["y"] - before["y"])
    off = math.hypot(after["x"] - want[0], after["y"] - want[1])
    case.check("a drag %s moves the map with the finger" % what,
               moved > 100.0 and off <= WORLD_SLACK,
               "(%.1f, %.1f) -> (%.1f, %.1f), the finger's ground at "
               "(%.1f, %.1f), %.1f off"
               % (before["x"], before["y"], after["x"], after["y"],
                  want[0], want[1], off))


def case_touch_automap(case: Case) -> None:
    """CMenuAutoMap native on touch: drag, hold, Rotate and Back."""
    case.check("the run reached the level's gameplay",
               into_level(case, *SHAPES[case.name]))
    case.http("/console?command=" + urllib.parse.quote("openmenu automap"))
    deadline = time.monotonic() + 20
    menu = {}
    while time.monotonic() < deadline:
        menu = read_menu(case)
        if menu.get("class") == "CMenuAutoMap" and len(buttons(menu)) >= 3:
            break
        time.sleep(0.5)
    case.check("the game's automap is up", menu.get("class") == "CMenuAutoMap",
               str(menu.get("class")))
    view = menu.get("automap")
    case.check("GET /menu reads the map's view", bool(view), str(view))
    if menu.get("class") != "CMenuAutoMap" or not view:
        case.shot("no-automap")
        return
    time.sleep(1.0)
    case.shot("open")
    controls = live_controls(case)
    case.check("no menu pad is drawn over it",
               not any(name.startswith("menu-") for name in controls),
               "drawn: %s" % sorted(controls))
    found = buttons(menu)
    named = ("back", "turn left", "turn right")
    case.check("Back and both Rotate buttons are finger buttons in the window",
               all(name in found and finger_tall(found[name])
                   and inside_window(found[name]) for name in named),
               str({name: found.get(name, {}).get("fingers")
                    for name in named}))
    areas = [found[name]["area"] for name in named if name in found]
    case.check("the buttons are disjoint",
               len(areas) == 3 and all(disjoint(a, b)
                                       for i, a in enumerate(areas)
                                       for b in areas[i + 1:]), str(areas))
    if "back" not in found:
        return
    scale = scene_per_window(found["back"])

    check_drag(case, "across", scale, 0.12, 0.0)
    check_drag(case, "up", scale, 0.0, -0.12)
    case.shot("dragged")

    before = automap(case)
    tap(case, {"x": 0.85, "y": 0.25}, hold=1.5)
    after = automap(case)
    case.check("a finger held still does not pan the map",
               abs(after["x"] - before["x"]) < 1e-3
               and abs(after["y"] - before["y"]) < 1e-3,
               "(%.1f, %.1f) -> (%.1f, %.1f)"
               % (before["x"], before["y"], after["x"], after["y"]))

    turns = {}
    for name in ("turn left", "turn right"):
        start = automap(case)["turn"]
        down = found[name]["edge"]
        case.http("/touch?x=%g&y=%g&phase=down" % (down["x"], down["y"]))
        time.sleep(0.2)
        held = automap(case)["turn"]
        if name == "turn left":
            case.shot("turning")
        case.http("/touch?x=%g&y=%g&phase=up" % (down["x"], down["y"]))
        time.sleep(0.3)
        turns[name] = held - start
    case.check("holding < Rotate turns the map as the camera's left does",
               turns["turn left"] > 0.0, "%+.3f rad" % turns["turn left"])
    case.check("holding Rotate > turns it the other way",
               turns["turn right"] < 0.0, "%+.3f rad" % turns["turn right"])

    tap(case, found["back"]["edge"])
    deadline = time.monotonic() + 10
    closed = False
    while time.monotonic() < deadline and not closed:
        closed = read_menu(case).get("active") is False
        time.sleep(0.3)
    case.check("a tap on Back's edge closes the automap", closed)
    case.shot("after")
