"""Live case for the gameplay action cluster on touch (docs/touch-play.md).

The run continues into the Sanctuary hub with touch forced on, reads the drawn
controls and the moved minimap from GET /controls, and judges the layout: the
buttons disjoint, inside the window, a finger or more each, clear of the
minimap, Attack the largest in the corner. It then taps each drawn button and
reads the game's own poll of the pad from the run's input recording, and taps
Use inside the trivia console's trigger, which opens the game's trivia menu.
Each variant runs in one window shape.
"""

from __future__ import annotations

import json
import time

from live_cases_extraction import script
from live_cases_use_prompt import (AWAY, HUB_MAP, TRIVIA, press, teleport,
                                   wait_menu, wait_prompt)
from live_game import live_controls, live_hud_box, live_viewport
from live_harness import ROOT, Case
from live_menu import keyboard_into_gameplay

SHAPES = {
    "touch-cluster": (2000, 927),
    "touch-cluster-16x9": (1280, 720),
    "touch-cluster-20x9": (1600, 720),
    "touch-cluster-21x9": (1680, 720),
    "touch-cluster-4x3": (960, 720),
    "touch-cluster-tablet": (1152, 720),
}
# The pad's DirectInput buttons the game reads, in its A, B, X, Y order.
FACE = {"a": 0, "b": 1, "x": 2, "y": 3}
EXPECTED = {
    "light-attack": ("a", False),
    "heavy-attack": ("b", False),
    "use": ("x", False),
    "jump": ("y", False),
    "power-1": ("a", True),
    "power-2": ("b", True),
    "power-3": ("x", True),
    "power-4": ("y", True),
}


def overlap(a, b) -> bool:
    return (a[0] < b[0] + b[2] and b[0] < a[0] + a[2]
            and a[1] < b[1] + b[3] and b[1] < a[1] + a[3])


def wait_minimap(case: Case, timeout: float):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        box = live_hud_box(case, "minimap")
        if box:
            return box
        time.sleep(0.3)
    return None


def recording(case: Case):
    live = json.loads((case.dir / "live.json").read_text())
    path = live.get("input_recording")
    # The run writes it relative to its own working directory, ROOT.
    return str(ROOT / path) if path else None


def pad_reads(path: str, start: int) -> list[dict]:
    with open(path, "rb") as f:
        f.seek(start)
        lines = f.read().decode(errors="replace").splitlines()
    out = []
    for line in lines:
        try:
            record = json.loads(line)
        except json.JSONDecodeError:
            continue
        if record.get("type") == "gamepad":
            out.append(record)
    return out


def tap_reaches_game(case: Case, path: str, rect, window, action: str):
    """Tap the button's centre; the game's own pad poll must have read its
    face button down, with the right trigger for a power and without it
    otherwise."""
    face, trigger = EXPECTED[action]
    before = pad_reads(path, 0)
    rest = before[-1]["axes"] if before else [0] * 8
    start = len(open(path, "rb").read())
    x = (rect[0] + rect[2] * 0.5) / window[0]
    y = (rect[1] + rect[3] * 0.5) / window[1]
    delivered = press(case, x, y)
    time.sleep(0.5)
    reads = pad_reads(path, start)
    hit = [r for r in reads if FACE[face] in r.get("buttons", [])]
    if not hit:
        return delivered, False, "%d pad read(s), none with %s" % (
            len(reads), face)
    # The right trigger is an axis: a power moves it, a plain button does not.
    pulled = hit[0]["axes"] != rest
    return delivered, pulled == trigger, "buttons %s axes %s (rest %s)" % (
        hit[0]["buttons"], hit[0]["axes"], rest)


def case_touch_cluster(case: Case) -> None:
    width, height = SHAPES[case.name]
    case.prepare_profile(["boot.mode=continue", "input.touch_controls=2",
                          "video.width=%d" % width,
                          "video.height=%d" % height,
                          "video.mode=windowed"])
    case.seed_save("autosave0.save")
    case.launch({"X2_FILES": "1"}, args=("--record-input",))
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

    minimap = wait_minimap(case, 10)
    case.check("the minimap is moved into the mobile layout", minimap,
               str(minimap))
    controls = live_controls(case)
    (case.dir / "controls.txt").write_text(case.get_text("/controls"))
    case.shot("cluster")
    if not minimap:
        return
    finger = 0.1 * min(window)
    buttons = {name: rect for name, rect in controls.items()
               if name in EXPECTED}
    case.check("Attack, Smash, Jump and Use are drawn",
               {"light-attack", "heavy-attack", "jump", "use"} <= set(buttons),
               str(sorted(buttons)))
    attack = buttons.get("light-attack")
    if not attack:
        return
    names = sorted(buttons)
    touching = ["%s/%s" % (a, b) for i, a in enumerate(names)
                for b in names[i + 1:] if overlap(buttons[a], buttons[b])]
    case.check("no two buttons overlap", not touching, str(touching))
    outside = [n for n, (x, t, w, h) in buttons.items()
               if x < 0 or t < 0 or x + w > window[0] or t + h > window[1]]
    case.check("every button is inside the %gx%g window" % window,
               not outside, str(outside))
    small = ["%s %gx%g" % (n, r[2], r[3]) for n, r in buttons.items()
             if r[2] < finger or r[3] < finger]
    case.check("every button is a finger (%g px) or more" % finger,
               not small, str(small))
    covered = [n for n, r in controls.items() if overlap(r, minimap)]
    case.check("no drawn control overlaps the minimap %s" % (minimap,),
               not covered, str(covered))
    others = [r for n, r in buttons.items() if n != "light-attack"]
    case.check("Attack is the largest button",
               all(attack[2] > r[2] for r in others))
    case.check("Attack sits in the bottom-right corner",
               attack[0] + attack[2] > window[0] * 0.85
               and attack[1] + attack[3] > window[1] * 0.85)
    cluster_top = min(r[1] for r in buttons.values())
    cluster_left = min(r[0] for r in buttons.values())
    case.check("the minimap is up and to the right of the cluster",
               minimap[1] + minimap[3] <= cluster_top
               and minimap[0] >= cluster_left, "cluster top %g left %g" % (
                   cluster_top, cluster_left))

    path = recording(case)
    case.check("the run records the input the game reads", path, str(path))
    if not path:
        return
    for name in sorted(buttons):
        if name == "use":
            continue
        delivered, ok, detail = tap_reaches_game(case, path, buttons[name],
                                                 window, name)
        case.check("a tap on %s is delivered and the game reads it" % name,
                   delivered and ok, detail)
        time.sleep(0.8)
    teleport(case, TRIVIA)
    prompt = wait_prompt(case, 10)
    case.check("the trivia trigger shows the use prompt", prompt, str(prompt))
    (case.dir / "controls-prompt.txt").write_text(case.get_text("/controls"))
    case.shot("cluster-prompt")
    use = live_controls(case).get("use")
    case.check("Use is still drawn beside the use prompt", use is not None)
    if not use:
        return
    x = (use[0] + use[2] * 0.5) / window[0]
    y = (use[1] + use[3] * 0.5) / window[1]
    case.check("a finger on the cluster's Use is delivered", press(case, x, y))
    shown = wait_menu(case, "trivia", 10)
    case.check("Use opened the trivia menu", shown == "trivia", shown)
    case.shot("trivia")
