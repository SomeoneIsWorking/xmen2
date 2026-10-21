"""Live cases for the extraction point: the revive (`gameplay.extraction_revive`) and the world map it opens.

The revive cases launch the real binary on genosha1, kills a hero with the game's
own killEntity, walks the party onto the extraction pad with setPos*, and
judges the result by GET /party, which reads health, energy and money from the
game's memory. See docs/RE/extraction.md.
"""

from __future__ import annotations

import json
import re
import time
import urllib.parse

from live_harness import Case
from live_game import png_mean_diff, wait_controls_unlocked
from live_cases_touch_menu import roster_hero
from live_menu import (keyboard_into_gameplay, menu_item, read_menu, tap_touch_button,
                       touch_button, touch_menu, wait_game_menu,
                       wait_settled_touch_menu)

PAD_MAP = "act1/genosha/genosha1"
ADD_MONEY = "act1/genosha/genosha1/temp_addmoney"
HEROES = 4
OFFSETS = ((0, 0), (0, 60), (-60, 0), (0, -60))
AWAY = (2700.0, 3400.0, -115.0)
BEHIND = (2680.0, 4271.0, -119.0)


def reach_pad_map(case: Case, mode: str) -> None:
    case.prepare_profile(["boot.mode=continue", "input.touch_controls=2",
                          "gameplay.extraction_revive=%s" % mode])
    case.seed_save("autosave0.save")
    case.launch({"X2_FILES": "1"})
    case.wait_control(60)
    menu = keyboard_into_gameplay(case, 300)
    case.check("the run reached gameplay through the PDA",
               menu.get("active") is False, str(menu.get("menu")))
    case.http("/console?command=loadmap%20" + PAD_MAP)
    time.sleep(5)
    menu = keyboard_into_gameplay(case, 300)
    case.check("loadmap %s reached gameplay" % PAD_MAP,
               menu.get("active") is False, str(menu.get("menu")))


def script(case: Case, text: str) -> None:
    case.http("/console?command=runscript%20" + urllib.parse.quote(text))
    time.sleep(0.4)


def party(case: Case) -> dict:
    code, body = case.http("/party")
    return json.loads(body) if code == 200 else {}


def fallen(state: dict) -> list[str]:
    return [h["definition"] for h in state.get("heroes", [])
            if h["health"] <= 0]


def full(state: dict) -> bool:
    heroes = state.get("heroes", [])
    return len(heroes) == HEROES and all(
        h["health"] == h["max_health"] and h["energy"] == h["max_energy"]
        for h in heroes)


def place(case: Case, at: tuple[float, float, float],
          heroes: tuple[int, ...], leaving: bool = False) -> None:
    """Teleport heroes around `at`, each hero's X, Y and Z in turn and twice,
    as one setPos is not always taken. Leaving sets Y before X, so a hero
    never crosses the pad on the way out."""
    axes = ("Y", "X") if leaving else ("X", "Y")
    centre = {"X": at[0], "Y": at[1]}
    for hero in heroes * 2:
        for axis in axes:
            offset = OFFSETS[hero - 1][0 if axis == "X" else 1]
            script(case, "setPos%s('_HERO%d_',%d)" % (axis, hero,
                                                    centre[axis] + offset))
        script(case, "setPosZ('_HERO%d_',%d)" % (hero, at[2]))
    time.sleep(2)


def put_party_on_pad(case: Case) -> dict:
    """Heroes 1, 3 and 4 onto the pad, invulnerable because the pad's
    enemies would kill them mid-case; hero 2 stays behind, because a living
    ally within the retail nearby radius stands him back up."""
    pads = party(case).get("pads", [])
    case.check("the map has one extraction pad", len(pads) == 1, str(pads))
    pad = pads[0]
    place(case, (pad["x"], pad["y"], pad["z"] + 4), (1, 3, 4))
    for _ in range(2):
        script(case, "setPosX('_ACTIVE_HERO_',%d)" % pad["x"])
        script(case, "setPosY('_ACTIVE_HERO_',%d)" % pad["y"])
    for hero in (1, 3, 4):
        script(case, "setInvulnerable('_HERO%d_','TRUE')" % hero)
    time.sleep(2)
    return pad


def fell_hero_two(case: Case, money: bool = True) -> None:
    """Money in the pocket and hero 2 down, before anybody is moved: a kill
    sent after the party is teleported does not take."""
    if money:
        case.http("/console?command=runscript%20" + ADD_MONEY)
        time.sleep(0.5)
    script(case, 'killEntity("_HERO2_")')
    time.sleep(1)
    state = party(case)
    case.check("hero 2 is fallen", len(fallen(state)) == 1, str(fallen(state)))


def wound_hero_three(case: Case) -> dict:
    """Hero 3 nearly down, once the party stands on the pad."""
    script(case, "setHealth('_HERO3_',7)")
    time.sleep(1)
    state = party(case)
    case.check("hero 3 is hurt, hero 1 and 4 are not",
               [h["health"] for h in state["heroes"]][2] < 90.0, str(state))
    return state


def stage_party(case: Case, money: bool = True, wound: bool = True) -> None:
    fell_hero_two(case, money)
    put_party_on_pad(case)
    if wound:
        wound_hero_three(case)


def wait_full(case: Case) -> dict:
    state: dict = {}
    for _ in range(60):
        state = party(case)
        if full(state):
            break
        time.sleep(0.25)
    return state


def restores(case: Case) -> int:
    return case.log_text().count("free: restored")


def fell_again(case: Case) -> None:
    """killEntity on hero 2 until he is down; a kill sent while he is still
    getting up from the last revive is lost."""
    for _ in range(4):
        script(case, 'killEntity("_HERO2_")')
        for _ in range(12):
            if fallen(party(case)):
                return
            time.sleep(0.25)


def leave_pad(case: Case) -> None:
    """Everyone off the pad, hero 2 apart from the others: a living hero
    within the retail nearby radius of a body stands it back up. The AI heroes
    3 and 4 go first and the controlled hero 1 last, because an AI hero left
    behind wanders across the pad's edge, and stepping back on is a fresh
    touch."""
    place(case, AWAY, (3, 4, 1), leaving=True)
    place(case, BEHIND, (2,), leaving=True)
    time.sleep(3)


def stand_beside_pad(case: Case) -> None:
    """Heroes 1, 3 and 4 250 units off the pad: inside the retail nearby-revive
    radius the bridge once used, outside the pad's 120-unit touch box."""
    pad = party(case)["pads"][0]
    place(case, (pad["x"], pad["y"] + 250, pad["z"] + 4), (1, 3, 4))
    for hero in (1, 3, 4):
        script(case, "setInvulnerable('_HERO%d_','TRUE')" % hero)
    time.sleep(2)


def case_extraction_free(case: Case) -> None:
    """free: stepping onto the pad restores the party once, and again only
    after every hero has left and come back; standing beside it does not."""
    reach_pad_map(case, "free")
    fell_hero_two(case)
    case.check("nothing is restored while the party is far from the pad",
               restores(case) == 0)
    stand_beside_pad(case)
    clear_cinematic(case)
    beside = party(case)
    case.check("standing beside the pad restores nothing",
               restores(case) == 0 and len(fallen(beside)) == 1,
               "%s restores %d" % (fallen(beside), restores(case)))
    put_party_on_pad(case)
    clear_cinematic(case)
    entered = wait_full(case)
    case.check("stepping onto the pad revives and refills the party, no command",
               full(entered), str(entered.get("heroes")))
    case.check("the bridge restored the party once",
               restores(case) == 1 and "free: restored 4 party heroes (1 "
               "revived)" in case.log_text())
    case.check("stepping onto the pad autosaves",
               wait_pad_autosaves(case, 1), autosave_lines(case))
    time.sleep(8)
    fell_again(case)
    down = party(case)
    case.check("hero 2 is fallen on the pad", len(fallen(down)) == 1,
               str([(h["definition"], h["health"]) for h in down["heroes"]]))
    wound_hero_three(case)
    time.sleep(3)
    inside = party(case)
    case.check("staying inside restores nothing again",
               len(fallen(inside)) == 1 and restores(case) == 1,
               "%s restores %d" % (fallen(inside), restores(case)))
    leave_pad(case)
    left = party(case)
    case.check("leaving restores nothing",
               len(fallen(left)) == 1 and restores(case) == 1,
               "%s restores %d" % (fallen(left), restores(case)))
    put_party_on_pad(case)
    clear_cinematic(case)
    again = wait_full(case)
    case.check("stepping back onto the pad autosaves again",
               wait_pad_autosaves(case, 2), autosave_lines(case))
    case.check("stepping back onto the pad restores the party again",
               full(again) and restores(case) == 2,
               "restores %d %s" % (restores(case), again.get("heroes")))
    case.shot("free-after")


def autosave_lines(case: Case) -> str:
    return str([line for line in case.log_text().splitlines()
                if "[autosave]" in line])


def pad_autosaves(case: Case) -> int:
    return len(re.findall(r"succeeded autosave\d\.save \(extraction pad\)",
                          case.log_text()))


def wait_pad_autosaves(case: Case, count: int) -> bool:
    deadline = time.monotonic() + 60
    while time.monotonic() < deadline and pad_autosaves(case) < count:
        time.sleep(0.2)
    return pad_autosaves(case) == count


def choose_team(case: Case) -> dict:
    """The choose-team load of the pad map, which moves a fallen party hero
    to the bench (docs/RE/menus.md); returns the team menu's touch model."""
    case.http("/console?command=loadmap%20" + PAD_MAP + "%200%201")
    return wait_settled_touch_menu(case, "team", 120)


def benched_hero(case: Case, shown: dict, internal: str) -> dict:
    """`internal`'s roster record, read with the roster open through Replace
    on the empty party slot, then back to the party."""
    rows = [r["label"] for r in shown.get("rows", [])]
    if "Empty slot" not in rows:
        return {}
    empty = rows.index("Empty slot")
    if shown.get("focused_row") != empty:
        tap_touch_button(case, shown, touch_button(shown, "row", "empty slot"))
        shown = wait_game_menu(case, lambda m: m.get("touch_menu", {}).get(
            "focused_row") == empty, 10).get("touch_menu", {})
    tap_touch_button(case, shown, touch_button(shown, "footer", "replace"))
    wait_game_menu(case, lambda m: m.get("mode") == 1, 10)
    hero = roster_hero(case, internal)
    case.http("/key?name=Escape&hold=0.2")
    wait_game_menu(case, lambda m: m.get("mode") == 0, 10)
    return hero


def accept_team(case: Case) -> None:
    shown = touch_menu(case)
    accept = touch_button(shown, "footer", "accept")
    case.check("the team menu offers Accept", accept is not None,
               str([b["label"] for b in shown.get("buttons", [])]))
    if accept is not None:
        tap_touch_button(case, shown, accept)
    left = wait_game_menu(case, lambda m: m.get("menu") != "team", 20)
    case.check("Accept left the team menu", left.get("menu") != "team",
               str(left.get("menu")))
    keyboard_into_gameplay(case, 120)


def case_extraction_free_bench(case: Case) -> None:
    """free: a fallen hero on the bench, who has no actor in the world, is
    revived too when the party steps onto the pad."""
    reach_pad_map(case, "free")
    fell_hero_two(case)
    benched = benched_hero(case, choose_team(case), "cyclops")
    case.check("the choose-team load benched fallen Cyclops",
               benched.get("fallen") is True, str(benched))
    accept_team(case)
    state = party(case)
    case.check("the party is the three standing heroes",
               len(state.get("heroes", [])) == 3 and not fallen(state),
               str([h["definition"] for h in state.get("heroes", [])]))
    put_party_on_pad(case)
    clear_cinematic(case)
    time.sleep(2)
    case.check("the bridge revived one character off the field",
               "revived 1 of " in case.log_text())
    after = benched_hero(case, choose_team(case), "cyclops")
    case.check("Cyclops stands on the bench after the pad",
               after.get("fallen") is False, str(after))
    case.shot("bench-after")


def case_extraction_off(case: Case) -> None:
    """off: the same sequence changes nothing, the hero stays down."""
    reach_pad_map(case, "off")
    stage_party(case)
    clear_cinematic(case)
    time.sleep(4)
    after = party(case)
    case.check("the fallen hero stays down", len(fallen(after)) == 1,
               str(fallen(after)))
    case.check("the hurt hero stays hurt",
               after["heroes"][2]["health"] < after["heroes"][2]["max_health"],
               str(after.get("heroes")))
    case.check("no offer is made", after.get("offered") is False)
    case.check("the bridge did nothing",
               "free: restored" not in case.log_text()
               and "paid:" not in case.log_text())


def clear_cinematic(case: Case) -> None:
    """Arriving at the pad starts the map's Lady Deathstrike scene; Escape
    skips it, and the prompt only shows once the player has control."""
    released = "controls released; conversation payload inactive"
    if released not in case.get_text("/input?controller=0"):
        case.http("/key?name=Escape&hold=0.4")
    case.check("the party has control at the pad",
               wait_controls_unlocked(case, 40))
    time.sleep(1)


def wait_offer(case: Case, text: str) -> dict:
    """Poll /party until the prompt offers `text` (a substring)."""
    state: dict = {}
    for _ in range(40):
        state = party(case)
        if state.get("offered") is not None and text in state.get("offer", ""):
            return state
        time.sleep(0.25)
    return state


def offered_total(offer: str) -> int:
    """The number the prompt shows after its colon."""
    return int(offer.split(":")[1].split()[0])


def wait_revived(case: Case) -> dict:
    state: dict = {}
    for _ in range(40):
        state = party(case)
        if not fallen(state):
            break
        time.sleep(0.25)
    return state


def request_f3(case: Case) -> None:
    case.http("/ui/key?name=F3")


def request_lb(case: Case) -> None:
    case.http("/pad?button=leftshoulder&hold=0.2")


def request_tap(case: Case) -> None:
    case.http("/touch?x=0.5&y=0.17&phase=down")
    time.sleep(0.1)
    case.http("/touch?x=0.5&y=0.17&phase=up")


def revive_by(case: Case, how: str, request) -> None:
    """Hero 2 is down again: one request pays the offer and stands him up.
    The previous revive's get-up timer would undo a kill sent too soon."""
    time.sleep(5)
    script(case, 'killEntity("_HERO2_")')
    for _ in range(20):
        if fallen(party(case)):
            break
        time.sleep(0.25)
    before = wait_offer(case, "Revive 1 fallen hero")
    offer = before.get("offer", "")
    case.check("%s: the prompt offers the revive with its total" % how,
               "Revive 1 fallen hero:" in offer and before.get("offered"),
               offer)
    total = offered_total(offer)
    request(case)
    after = wait_revived(case)
    case.check("%s: the fallen hero is up" % how, not fallen(after),
               str(fallen(after)))
    case.check("%s: money fell by exactly the offered total %d" % (how, total),
               before["money"] - after["money"] == total,
               "%s -> %s" % (before["money"], after["money"]))


def case_extraction_paid(case: Case) -> None:
    """paid: near the pad the prompt shows the total, a request pays it."""
    reach_pad_map(case, "paid")
    stage_party(case, money=False, wound=False)
    clear_cinematic(case)
    broke = wait_offer(case, "not enough money")
    case.check("with no money the prompt says so",
               "not enough money" in broke.get("offer", "")
               and broke.get("offered") is True, broke.get("offer", ""))
    request_f3(case)
    time.sleep(1.5)
    refused = party(case)
    case.check("a refused request shows its reason and takes nothing",
               "Not enough money" in refused.get("notice", "")
               and refused["money"] == broke["money"]
               and len(fallen(refused)) == 1,
               "%s money %s" % (refused.get("notice"), refused["money"]))
    case.http("/console?command=runscript%20" + ADD_MONEY)
    time.sleep(1)
    revive_by(case, "F3", request_f3)
    revive_by(case, "controller LB", request_lb)
    revive_by(case, "touch tap", request_tap)
    log = case.log_text()
    case.check("the bridge logged three paid revives",
               log.count("paid: revived 1 for") == 3)
    case.shot("paid-after")

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
    """The world map an extraction point opened shows one Back and go, Escape
    leaves it, and the party answers the keyboard again."""
    case.prepare_profile(["boot.mode=continue"])
    case.seed_save("autosave0.save")
    case.launch({"X2_FILES": "1"})
    case.wait_control(60)
    shown = reach_world_map_by_pad(case)
    case.check("the pad opened the world map",
               shown.get("menu") == "worldmap", str(shown.get("menu")))
    case.shot("worldmap")
    if shown.get("menu") != "worldmap":
        return
    time.sleep(1.0)
    primary = menu_item(case, "desctext1")
    secondary = menu_item(case, "desctext2")
    goes = menu_item(case, "desctext4")
    case.check("the first Back prompt is shown",
               "$MENU_BACK" in primary.get("label", "")
               and not primary.get("flags", 4) & 4, str(primary))
    case.check("the second Back prompt is hidden",
               "$MENU_BACK" in secondary.get("label", "")
               and bool(secondary.get("flags", 0) & 4), str(secondary))
    case.check("go is shown",
               "$MENU_ACCEPT" in goes.get("label", "")
               and not goes.get("flags", 4) & 4, str(goes))
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
