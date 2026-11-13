"""Live case for the money notice in the mobile HUD (external/xmen2-decomp/docs/notes/hud.md).

CHud shows money, its icon and "total + picked up", for a few seconds after
the party picks money up (005a5170). The run loads Dead Zone 2 on touch,
kills spawned Mikhail Rangers until one drops money, puts the active hero on
the drop, and judges the notice: GET /controls reports where it was drawn,
and no drawn control (the potion buttons included) may overlap it.
"""

from __future__ import annotations

import time
import urllib.parse

from live_cases_pickups import ENEMY, LEVEL, pickups, run_script
from live_game import live_controls, live_hud_box
from live_harness import Case
from live_menu import keyboard_into_gameplay

SHAPES = {
    "touch-money": (1096, 720),
    "touch-money-20x9": (1600, 720),
}
WAVES = 6
ENEMIES = 6


def overlaps(a, b) -> bool:
    return (a[0] < b[0] + b[2] and b[0] < a[0] + a[2]
            and a[1] < b[1] + b[3] and b[1] < a[1] + a[3])


def wait_box(case: Case, name: str, timeout: float):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        box = live_hud_box(case, name)
        if box:
            return box
        time.sleep(0.1)
    return None


def money_drop(case: Case) -> dict | None:
    before = {p["handle"] for p in pickups(case).get("pickups", [])}
    for wave in range(WAVES):
        for index in range(ENEMIES):
            run_script(case, "spawn('_ACTIVE_HERO_','%s','x2_money%d_%d',"
                       "' %d 0 0 ',' 0 0 0 ')"
                       % (ENEMY, wave, index, 120 + 30 * index))
        time.sleep(1.0)
        for index in range(ENEMIES):
            run_script(case, "killEntity('x2_money%d_%d')" % (wave, index))
        time.sleep(6.0)
        for drop in pickups(case).get("pickups", []):
            if (drop["handle"] not in before
                    and drop["definition"] == "inventory_money"):
                return drop
    return None


def case_touch_money(case: Case) -> None:
    width, height = SHAPES[case.name]
    case.prepare_profile(["boot.mode=continue", "input.touch_controls=2",
                          "video.width=%d" % width,
                          "video.height=%d" % height,
                          "video.mode=windowed"])
    case.seed_save("autosave0.save")
    case.launch({"X2_FILES": "1"})
    case.wait_control(60)
    menu = keyboard_into_gameplay(case, 300)
    case.check("the run reached gameplay", menu.get("active") is False,
               str(menu.get("menu")))
    case.http("/console?command=" + urllib.parse.quote("loadmap " + LEVEL))
    time.sleep(8)
    menu = keyboard_into_gameplay(case, 300)
    case.check("loadmap %s reached gameplay" % LEVEL,
               menu.get("active") is False, str(menu.get("menu")))
    run_script(case, "setInvulnerable('_ACTIVE_HERO_','TRUE')")
    drop = money_drop(case)
    case.check("a killed enemy dropped money", drop is not None)
    if not drop:
        return
    money = None
    # Drops scatter after they land, so each try reads where one is now.
    for _ in range(4):
        drops = [p for p in pickups(case).get("pickups", [])
                 if p["definition"] == "inventory_money"]
        if not drops:
            break
        at = (drops[0]["x"], drops[0]["y"], drops[0]["z"])
        # One script, so the notice's few seconds start while this case polls.
        run_script(case, "\n".join("setPos%s('_ACTIVE_HERO_',%g)" % (a, v)
                                    for a, v in zip("XYZ", at, strict=True)))
        money = wait_box(case, "money", 3)
        if money:
            break
    case.shot("money")
    case.check("picking the money up shows the notice", money is not None)
    if not money:
        return
    controls = live_controls(case)
    case.check("the potion buttons are drawn",
               "health-pack" in controls and "energy-pack" in controls,
               str(sorted(controls)))
    hit = [name for name, rect in controls.items() if overlaps(money, rect)]
    case.check("no drawn control overlaps the notice %s" % (money,),
               not hit, str(hit))
