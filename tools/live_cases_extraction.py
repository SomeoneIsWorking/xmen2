"""Live cases for the extraction point: the revive (`gameplay.extraction_revive`) and the world map it opens.

The revive cases launch the real binary on genosha1, kills a hero with the game's
own killEntity, walks the party onto the extraction pad with setPos*, and
judges the result by GET /party, which reads health, energy and money from the
game's memory. See docs/RE/extraction.md.
"""

from __future__ import annotations

import json
import time
import urllib.parse

from live_harness import Case
from live_game import png_mean_diff, wait_controls_unlocked
from live_menu import keyboard_into_gameplay, menu_item, read_menu, wait_game_menu

PAD_MAP = "act1/genosha/genosha1"
ADD_MONEY = "act1/genosha/genosha1/temp_addmoney"
HEROES = 4
OFFSETS = ((0, 0), (0, 60), (-60, 0), (0, -60))
AWAY = (2700.0, 3400.0, -115.0)
BEHIND = (2680.0, 4271.0, -119.0)


def reach_pad_map(case: Case, mode: str) -> None:
    case.prepare_profile(["boot.mode=continue", "input.touch_controls=2",
                          "gameplay.extraction_revive=%s" % mode])
    case.seed_save("autosave.save")
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
    never stands inside the radius on the way out."""
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
    """Everyone outside the 480 radius, hero 2 apart from the others: a living
    hero within the retail nearby radius of a body stands it back up."""
    place(case, AWAY, (1, 3, 4), leaving=True)
    place(case, BEHIND, (2,), leaving=True)
    time.sleep(3)


def case_extraction_free(case: Case) -> None:
    """free: walking into the pad's radius restores the party once, and again
    only after every hero has left and come back."""
    reach_pad_map(case, "free")
    fell_hero_two(case)
    case.check("nothing is restored while the party is far from the pad",
               restores(case) == 0)
    put_party_on_pad(case)
    clear_cinematic(case)
    entered = wait_full(case)
    case.check("entering the radius revives and refills the party, no command",
               full(entered), str(entered.get("heroes")))
    case.check("the bridge restored the party once",
               restores(case) == 1 and "free: restored 4 heroes, 1 of them "
               "revived" in case.log_text())
    time.sleep(8)
    fell_again(case)
    down = party(case)
    case.check("hero 2 is fallen inside the radius", len(fallen(down)) == 1,
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
    case.check("re-entering the radius restores the party again",
               full(again) and restores(case) == 2,
               "restores %d %s" % (restores(case), again.get("heroes")))
    case.shot("free-after")


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
