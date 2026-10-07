"""Live cases for the extraction point and the world map it opens."""

from __future__ import annotations

import time

from live_game import png_mean_diff
from live_harness import Case
from live_menu import keyboard_into_gameplay, read_menu, wait_game_menu

SANCTUARY = "act1/sanctuary/sanctuary1"
# Sanctuary's pad is down-left of the party's start; these holds put the
# active hero on it (measured on the continue save's autosave).
PAD_WALK = (("a", 1.4), ("s", 0.5))
# Mean grey difference over the frame that one second of walking leaves.
MOVED = 2.0


def reach_world_map_by_pad(case: Case) -> dict:
    """Continue boot, Sanctuary, walk to the pad, Use it, choose Xtract."""
    menu = keyboard_into_gameplay(case, 300)
    case.check("the run reached gameplay through the PDA",
               menu.get("active") is False, str(menu.get("menu")))
    case.http("/console?command=loadmap%20" + SANCTUARY)
    time.sleep(8)
    menu = keyboard_into_gameplay(case, 300)
    case.check("loadmap %s reached gameplay" % SANCTUARY,
               menu.get("active") is False, str(menu.get("menu")))
    time.sleep(6.0)
    case.shot("arrived")
    for key, hold in PAD_WALK:
        case.http("/key?name=%s&hold=%s" % (key, hold))
        time.sleep(1.0)
    case.shot("on_pad")
    case.http("/key?name=e&hold=0.2")
    popup = wait_game_menu(case, lambda m: m.get("popup") is True, 10)
    case.check("Use on the pad opened the Xtraction popup",
               popup.get("popup") is True, str(popup.get("menu")))
    case.http("/key?name=Return&hold=0.2")
    return wait_game_menu(case, lambda m: m.get("menu") == "worldmap", 20)


def case_extract_keyboard_back(case: Case) -> None:
    """Escape leaves the world map an extraction point opened, and the party
    answers the keyboard again."""
    case.prepare_profile(["boot.mode=continue"])
    case.seed_save("autosave.save")
    case.launch({"X2_FILES": "1"})
    case.wait_control(60)
    shown = reach_world_map_by_pad(case)
    case.check("the pad opened the world map",
               shown.get("menu") == "worldmap", str(shown.get("menu")))
    case.shot("worldmap")
    if shown.get("menu") != "worldmap":
        return
    time.sleep(1.0)
    case.http("/key?name=Escape&hold=0.2")
    left = wait_game_menu(case, lambda m: m.get("menu") != "worldmap", 10)
    case.check("Escape left the world map", left.get("menu") != "worldmap",
               str(left.get("menu")))
    time.sleep(1.0)
    before = case.shot("before_walk")
    case.http("/key?name=d&hold=1.0")
    time.sleep(0.5)
    after = case.shot("after_walk")
    moved = png_mean_diff(before, after)
    case.check("the party walks again", moved > MOVED, "diff %.2f" % moved)
    case.check("no menu is up", read_menu(case).get("active") is False)
