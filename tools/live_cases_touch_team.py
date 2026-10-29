"""Live cases for a finger on CMenuTeam, drawn natively on touch: the party,
the roster and the hero detail tabs with their finger column.

Every tap lands on a finger button at its edge, outside the game's own box,
and is judged by the game's own state: the menu's mode, its items' values,
the skill being assigned, a hero's power slots."""

from __future__ import annotations

import json
import time

from live_cases_touch_menu import (
    money,
    roster_hero,
    roster_names,
    shows_new_count,
    wait_labels,
    wait_tabs,
)
from live_harness import Case
from live_menu import (
    dismiss_level_up,
    finger_tall,
    footer_button,
    item_button,
    keyboard_into_gameplay,
    keyboard_into_the_pda,
    list_cards,
    menu_item,
    menu_labels,
    menu_list,
    menu_row,
    menu_text,
    read_menu,
    read_menu_items,
    select_list_entry,
    tap_button_edge,
    tap_footer_button,
    tap_list_entry,
    tap_menu_item,
    tap_tab,
    wait_game_menu,
    wait_list,
    wait_settled_menu,
)

ADD_MONEY = "runscript act1/genosha/genosha1/temp_addmoney"
# Every playable hero to level 40, with points to spend and any gear wearable.
LEVEL_UP = "runscript awardXPToPlayable(2000000)"
DETAIL_TABS = ["stats", "skills", "gear", "ai"]
STATS = ["body", "focus", "strike", "speed"]
AI = ["ai_heal_pickup", "ai_heal", "ai_level", "ai_power", "ai_auto_traits",
      "ai_auto_skills", "ai_auto_equip"]
# The assigning column's buttons and the slot each fills (assign_icon01..03:
# the diamond's bottom, right and left).
SLOT_BUTTONS = {"Right slot": 1, "Left slot": 2, "Bottom slot": 0}
PARTY = ["char_summary0%d" % n for n in range(1, 5)]
# FUN_005f01e0's rank glyphs: owned, added this visit, open, locked.
RANK_GLYPHS = "\xd9\xda\xd8\xd7"
# A scene unit in window heights at 16:9 (the plane is 384 units tall).
SCENE_HEIGHT = 384.0
# A finger in scene units in a landscape window: a tenth of the height.
FINGER = 0.1 * SCENE_HEIGHT
# CMenuItemSkills draws its first row this far below its box's top (measured:
# a tap there selects the row).
SKILL_ROWS_DOWN = 67.0
# The least clear space between a card's name and its value, scene units.
TEXT_GAP = 3.0
# The share of an option-art button's width its slanted right end covers at
# the text's height (measured: 0.11 to 0.14 in 16:9 screenshots).
ART_SLANT = 0.14
# A case named with one of these suffixes runs in a window of that aspect:
# 4:3, a 1.52 tablet and a 20:9 phone; 16:9 without one.
WINDOWS = {
    "-4x3": (960, 720),
    "-tablet": (1216, 800),
    "-20x9": (1600, 720),
}
# The cases run at every aspect.
ASPECT_CASES = ("touch-team-stats", "touch-team-skills", "touch-roster")


def console(case: Case, command: str) -> None:
    case.http("/console?command=" + command.replace(" ", "%20"))
    time.sleep(0.5)


def prepare_touch_profile(case: Case) -> None:
    """A Continue boot with touch controls, in the window the case's name
    asks for."""
    conf = ["boot.mode=continue", "input.touch_controls=2"]
    window = next((size for suffix, size in WINDOWS.items()
                   if case.name.endswith(suffix)), None)
    if window:
        conf += ["video.width=%d" % window[0], "video.height=%d" % window[1],
                 "video.mode=windowed"]
    case.prepare_profile(conf)


def start_levelled(case: Case, extra: tuple[str, ...] = ()) -> bool:
    prepare_touch_profile(case)
    case.seed_save("autosave0.save")
    case.launch({"X2_FILES": "1"})
    case.wait_control(60)
    menu = keyboard_into_gameplay(case, 300)
    case.check("the run reached gameplay through the PDA",
               menu.get("active") is False, str(menu.get("menu")))
    for command in (*extra, LEVEL_UP):
        console(case, command)
    dismiss_level_up(case)
    return menu.get("active") is False


def buttons_named(menu: dict, name: str) -> list[dict]:
    return [b for b in menu.get("touch_buttons", []) if b.get("name") == name]


def item_in(menu: dict, name: str) -> dict:
    return next((i for i in menu.get("items", []) if i["name"] == name), {})


def button_on(menu: dict, item: str, name: str) -> dict:
    """The finger button called `name` whose press lands on `item`'s box."""
    rect = item_in(menu, item).get("rect")
    if rect is None:
        return {}
    for button in buttons_named(menu, name):
        if (rect["left"] <= button["aim_x"] <= rect["right"]
                and rect["top"] <= button["aim_z"] <= rect["bottom"]):
            return button
    return {}


def tap_named(case: Case, name: str, item: str | None = None) -> bool:
    """Tap the finger button `name` (on `item`'s box) at its edge."""
    menu = read_menu_items(case)
    button = button_on(menu, item, name) if item else next(
        iter(buttons_named(menu, name)), {})
    return tap_button_edge(case, button)


def lit(menu: dict, name: str) -> bool:
    return bool(item_in(menu, name).get("flags", 0) & 0x01)


def footer_word(menu: dict, word: str) -> dict:
    """The finger button over the footer whose text ends with `word`."""
    for text in menu.get("texts", []):
        if text["name"].startswith("desctext") and \
                menu_text(text["label"]).lower() == word:
            return item_button(menu, text)
    return {}


def tap_footer_word(case: Case, word: str) -> bool:
    return tap_button_edge(case, footer_word(read_menu(case), word))


def lit_hero(menu: dict) -> str | None:
    return next((n for n in PARTY if lit(menu, n)), None)


def open_details(case: Case, tab: str) -> dict:
    """Open the team menu, the selected hero's details by a tap on its card
    and `tab` by a tap on it; the menu's items once it shows."""
    console(case, "openmenu team")
    party = wait_game_menu(case, lambda m: m.get("mode") == 0, 20)
    case.check("the team menu opened on its party",
               party.get("menu") == "team" and party.get("mode") == 0,
               "%s mode %s" % (party.get("menu"), party.get("mode")))
    items = read_menu_items(case)
    hero = lit_hero(items) or PARTY[0]
    tap_menu_item(case, item_in(items, hero))
    wait_game_menu(case, lambda m: m.get("mode") == 2, 5)
    if read_menu(case).get("mode") != 2:
        tap_menu_item(case, item_in(items, hero))
    stats = wait_game_menu(case, lambda m: m.get("mode") == 2, 10)
    case.check("a tap on the selected hero's card opened its stats",
               stats.get("mode") == 2, "mode %s" % stats.get("mode"))
    if tab != "stats" and stats.get("mode") == 2:
        tap_tab(case, r"detail_option0\d_text", tab)
        want = 2 + DETAIL_TABS.index(tab)
        opened = wait_game_menu(case, lambda m: m.get("mode") == want, 10)
        case.check("a tap on the %s tab's finger area opened it" % tab,
                   opened.get("mode") == want, "mode %s" % opened.get("mode"))
    time.sleep(1.0)
    return read_menu_items(case)


def leave_details(case: Case) -> None:
    """Accept (the game's Back) until the party shows: the gear pieces go
    back to the slots first."""
    party = read_menu(case)
    for _ in range(2):
        tap_footer_button(case, "$MENU_BACK")
        party = wait_game_menu(case, lambda m: m.get("mode") == 0, 5)
        if party.get("mode") == 0:
            break
    case.check("the details' Accept returned to the party",
               party.get("mode") == 0, "mode %s" % party.get("mode"))
    case.check("and the game is still running", case.alive())


def wait_labels_change(case: Case, names: list[str], before: dict,
                       timeout: float = 10) -> dict[str, str]:
    return wait_labels(case, lambda now: any(
        now.get(n) != before.get(n) for n in names), timeout)


def check_column(case: Case, menu: dict, what: str) -> None:
    """Every finger button is a finger across at least, and none of the
    column's overlaps another button (scene units; a finger is 38.4 at
    720p)."""
    buttons = [b for b in menu.get("touch_buttons", []) if "area" in b]
    areas = [b["area"] for b in buttons]
    small = [a for a in areas if min(a["right"] - a["left"],
                                     a["top"] - a["bottom"]) < 38.0]
    case.check("the %s screen's finger buttons are a finger at least"
               % what, bool(areas) and not small, str(small))
    overlap = [(a.get("name"), b.get("name"))
               for i, a in enumerate(buttons) for b in buttons[i + 1:]
               if (a.get("name") or b.get("name"))
               and a["area"]["left"] < b["area"]["right"]
               and b["area"]["left"] < a["area"]["right"]
               and a["area"]["bottom"] < b["area"]["top"]
               and b["area"]["bottom"] < a["area"]["top"]]
    case.check("and no column button overlaps another", not overlap,
               str(overlap))
    check_lettering(case, buttons, what)
    check_footers(case, menu, what)


def inside(inner: dict, outer: dict) -> bool:
    return (outer["left"] <= inner["left"] and inner["right"] <= outer["right"]
            and outer["bottom"] <= inner["bottom"]
            and inner["top"] <= outer["top"])


def overlaps(a: dict, b: dict) -> bool:
    return (a["left"] < b["right"] and b["left"] < a["right"]
            and a["bottom"] < b["top"] and b["bottom"] < a["top"])


def art_inside(area: dict) -> dict:
    """The part of a button's area its option art's panel fills: short of
    the slanted right end."""
    width = area["right"] - area["left"]
    return dict(area, right=area["right"] - ART_SLANT * width)


def check_lettering(case: Case, buttons: list[dict], what: str) -> None:
    """Each column button's letters land inside its art's panel, and a
    card's name ends a clear gap short of its value."""
    lettered = [b for b in buttons if "name_text" in b]
    outside = [b.get("name") for b in lettered
               if not inside(b["name_text"], art_inside(b["area"]))
               or ("value_text" in b
                   and not inside(b["value_text"], art_inside(b["area"])))]
    case.check("the %s column's text is inside its buttons" % what,
               bool(lettered) and not outside, str(outside))
    jammed = ["%s %.1f" % (b.get("name"),
                           b["value_text"]["left"] - b["name_text"]["right"])
              for b in lettered if "value_text" in b
              and b["value_text"]["left"] - b["name_text"]["right"]
              < TEXT_GAP]
    case.check("and each card's name stands clear of its value", not jammed,
               str(jammed))


def check_footers(case: Case, menu: dict, what: str) -> None:
    """The footer prompts' finger buttons do not overlap one another."""
    footers = [(menu_text(t["label"]), item_button(menu, t).get("area"))
               for t in menu.get("texts", [])
               if t["name"].startswith("desctext")]
    footers = [(label, area) for label, area in footers if label and area]
    overlap = [(a[0], b[0]) for i, a in enumerate(footers)
               for b in footers[i + 1:] if overlaps(a[1], b[1])]
    case.check("the %s screen's %d footer buttons are disjoint"
               % (what, len(footers)), not overlap, str(overlap))


def case_touch_team(case: Case) -> None:
    """CMenuTeam's party from the PDA: a tap on a hero's card selects it, a
    second opens its details, Accept leaves each."""
    case.prepare_profile(["boot.mode=continue", "input.touch_controls=2"])
    case.seed_save("autosave0.save")
    case.launch({"X2_FILES": "1"})
    case.wait_control(60)
    pda = keyboard_into_the_pda(case, 300)
    case.check("the run reached the PDA", pda.get("menu") == "pda",
               str(pda.get("menu")))
    team_row = menu_row(pda, "team management")
    case.check("the PDA has Team Management", bool(team_row))
    if not team_row:
        return
    tap_menu_item(case, team_row)
    team = wait_game_menu(case, lambda m: m.get("menu") == "team"
                          and m.get("mode") == 0, 20)
    case.check("the tap opened the game's team menu on its party",
               team.get("menu") == "team" and team.get("mode") == 0,
               "%s mode %s" % (team.get("menu"), team.get("mode")))
    time.sleep(1.0)
    items = read_menu_items(case)
    (case.dir / "team.json").write_text(json.dumps(items, indent=1) + "\n")
    heroes = [item_in(items, n).get("label") for n in PARTY]
    print("  party: %s" % heroes)
    case.shot("team")
    check_footers(case, items, "party")
    selected = lit_hero(items)
    target = PARTY[1] if selected != PARTY[1] else PARTY[0]
    tap_menu_item(case, item_in(items, target))
    picked = wait_game_menu(case, lambda m: lit_hero(read_menu_items(case))
                            == target, 10)
    case.check("a tap on %s's card selected that hero in the game"
               % item_in(items, target).get("label"),
               lit_hero(read_menu_items(case)) == target
               and picked.get("mode") == 0,
               "lit %s, mode %s" % (lit_hero(read_menu_items(case)),
                                    picked.get("mode")))
    case.shot("team-selected")
    tap_menu_item(case, item_in(items, target))
    details = wait_game_menu(case, lambda m: m.get("mode") == 2, 10)
    case.check("a second tap opened that hero's details",
               details.get("mode") == 2, "mode %s" % details.get("mode"))
    time.sleep(1.0)
    case.shot("details")
    leave_details(case)
    case.check("the party offers Accept as a finger button",
               bool(footer_button(read_menu(case), "$MENU_OK")))
    tap_footer_button(case, "$MENU_OK")
    left = wait_game_menu(case, lambda m: m.get("menu") != "team", 10)
    case.check("Accept left the team menu", left.get("menu") != "team",
               "now %r" % left.get("menu"))
    case.check("and the game is still running", case.alive())


def case_touch_team_stats(case: Case) -> None:
    """The stats column: a tap on a stat's card selects it without spending,
    + adds a point to it and - takes it back."""
    if not start_levelled(case):
        return
    menu = open_details(case, "stats")
    (case.dir / "stats.json").write_text(json.dumps(menu, indent=1) + "\n")
    labels = menu_labels(case)
    cards = [s for s in STATS if button_on(menu, "label_" + s, "select")]
    case.check("the column has a card for each of the game's four stats",
               cards == STATS, str(cards))
    case.check("each with its add and remove buttons",
               all(button_on(menu, "label_" + s, "add")
                   and button_on(menu, "label_" + s, "remove")
                   for s in STATS))
    check_column(case, menu, "stats")
    points = labels.get("points_stats")
    case.check("and points to spend", points not in (None, "0"), str(points))
    case.shot("stats")
    if cards != STATS or points in (None, "0"):
        return
    target = "strike" if not lit(menu, "label_strike") else "speed"
    before = menu_labels(case)
    tap_named(case, "select", "label_" + target)
    moved = wait_game_menu(case, lambda m: lit(read_menu_items(case),
                                               "label_" + target), 10)
    del moved
    now = menu_labels(case)
    case.check("a tap on %s's card selected it in the game" % target,
               lit(read_menu_items(case), "label_" + target))
    case.check("and spent nothing",
               all(now.get(s) == before.get(s) for s in STATS)
               and now.get("points_stats") == before.get("points_stats"),
               "%s -> %s" % ([before.get(s) for s in STATS],
                             [now.get(s) for s in STATS]))
    case.shot("stats-selected")
    other = "body" if target != "body" else "focus"
    before = menu_labels(case)
    tap_named(case, "add", "label_" + other)
    added = wait_labels_change(case, [other], before)
    case.check("+ on %s added a point to it, from the points left" % other,
               added.get(other) == str(int(before[other]) + 1)
               and added.get("points_stats")
               == str(int(before["points_stats"]) - 1),
               "%s %s -> %s %s" % (before.get(other),
                                   before.get("points_stats"),
                                   added.get(other),
                                   added.get("points_stats")))
    case.check("and selected it", lit(read_menu_items(case), "label_" + other))
    case.shot("stats-added")
    before = added
    tap_named(case, "remove", "label_" + other)
    removed = wait_labels_change(case, [other], before)
    case.check("- took that point back",
               removed.get(other) == str(int(before[other]) - 1)
               and removed.get("points_stats")
               == str(int(before["points_stats"]) + 1),
               "%s -> %s" % (before.get(other), removed.get(other)))
    leave_details(case)


def skill_open(box: dict, index: int) -> bool:
    """The skill has a rank the hero's level allows adding."""
    values = box.get("values", [])
    return 0 <= index < len(values) and RANK_GLYPHS[2] in values[index]


def skill_rank(box: dict, index: int) -> tuple[int, int]:
    """A skill's owned and total ranks, from its record's glyphs."""
    values = box.get("values", [])
    record = values[index] if 0 <= index < len(values) else ""
    owned = sum(record.count(g) for g in RANK_GLYPHS[:2])
    return owned, sum(record.count(g) for g in RANK_GLYPHS)


def tap_skill_row(case: Case, index: int) -> bool:
    """Tap the skill list's row `index` in the game's own list box."""
    item = menu_item(case, "skill_list")
    box = item.get("list_box", {})
    row = index - box.get("top", 0)
    if not 0 <= row < box.get("visible_rows", 0):
        return False
    rect, tap = item["rect"], item["tap"]
    centre = 0.5 * (rect["top"] + rect["bottom"])
    z = rect["bottom"] - SKILL_ROWS_DOWN - (row + 0.5) * box["row_height"]
    return case.http("/touch?x=%g&y=%g" % (
        tap["x"], tap["y"] + (centre - z) / SCENE_HEIGHT))[0] == 200


def case_touch_team_skills(case: Case) -> None:
    """The skills column: the selected skill's card with its rank, + adds a
    rank and - takes it back; a tap on a row of the list selects it."""
    if not start_levelled(case):
        return
    menu = open_details(case, "skills")
    (case.dir / "skills.json").write_text(json.dumps(menu, indent=1) + "\n")
    box = menu_list(case, "skill_list")
    case.check("the column has the skill's add and remove buttons",
               bool(buttons_named(menu, "add"))
               and bool(buttons_named(menu, "remove")))
    case.check("and the hero buttons",
               bool(buttons_named(menu, "Prev hero"))
               and bool(buttons_named(menu, "Next hero")))
    check_column(case, menu, "skills")
    case.check("the game's skill rows are a finger tall",
               box.get("row_height", 0) >= FINGER,
               "%s units, a finger %.1f" % (box.get("row_height"), FINGER))
    points = menu_labels(case).get("points_skills")
    case.check("and skill points to spend", points not in (None, "0"),
               str(points))
    case.shot("skills")
    target = next((i for i in range(len(box.get("entries", [])))
                   if i != box.get("selected") and skill_open(box, i)), None)
    if target is None or points in (None, "0"):
        case.check("a skill with a rank to add", False, str(box))
        return
    before = menu_labels(case)
    tap_skill_row(case, target)
    picked = wait_list(case, lambda b: b.get("selected") == target,
                       "skill_list")
    now = menu_labels(case)
    case.check("a tap on %s's row selected it" % box["entries"][target],
               picked.get("selected") == target, str(picked.get("selected")))
    case.check("and spent nothing",
               now.get("points_skills") == before.get("points_skills")
               and skill_rank(picked, target) == skill_rank(box, target),
               "%s -> %s" % (before.get("points_skills"),
                             now.get("points_skills")))
    case.shot("skills-selected")
    rank = skill_rank(picked, target)
    tap_named(case, "add")
    added = wait_list(case, lambda b: skill_rank(b, target) != rank,
                      "skill_list")
    now = menu_labels(case)
    case.check("+ added a rank to it, from the points left",
               skill_rank(added, target)[0] == rank[0] + 1
               and now.get("points_skills")
               == str(int(before["points_skills"]) - 1),
               "%s %s -> %s %s" % (rank, before.get("points_skills"),
                                   skill_rank(added, target),
                                   now.get("points_skills")))
    case.shot("skills-added")
    check_footers(case, read_menu(case), "skills with a rank added")
    tap_named(case, "remove")
    removed = wait_list(case, lambda b: skill_rank(b, target)[0] == rank[0],
                        "skill_list")
    case.check("- took the rank back",
               skill_rank(removed, target)[0] == rank[0]
               and menu_labels(case).get("points_skills")
               == before.get("points_skills"),
               str(skill_rank(removed, target)))
    leave_details(case)


def case_touch_team_gear(case: Case) -> None:
    """The gear tab's two lists as cards: a slot's card opens the pieces
    that fit, a piece's card equips it, Unequip takes it off and Drop (the
    pad's RB) drops one. The gear is bought first in the native shop."""
    if not start_levelled(case, (ADD_MONEY, ADD_MONEY, ADD_MONEY)):
        return
    console(case, "openmenu shop")
    wait_settled_menu(case, "shop", 20)
    tap_tab(case, r"shop_option0\d", "buy")
    wait_tabs(case, r"shop_option0\d", lambda t: t.get("buy") is True)
    bought = []
    for _ in range(2):
        entries = [menu_text(e) for e in menu_list(case).get("entries", [])]
        gear = next((i for i, e in enumerate(entries)
                     if "Pack" not in e and e != "Grab Bag"
                     and e not in bought), -1)
        if gear < 0:
            break
        select_list_entry(case, gear)
        before = menu_labels(case)
        tap_list_entry(case, gear)
        # The shop blanks its count for one frame before writing the new one.
        wait_labels(case, lambda now, before=before: shows_new_count(now, before))
        bought.append(entries[gear])
    count = menu_labels(case).get("inventory_count")
    case.check("two pieces of gear bought through the touch shop",
               count == "2/20", "%s, %s" % (bought, count))
    tap_footer_button(case, "$MENU_OK")
    wait_game_menu(case, lambda m: m.get("menu") != "shop", 10)
    menu = open_details(case, "gear")
    (case.dir / "gear.json").write_text(json.dumps(menu, indent=1) + "\n")
    slots = menu_list(case, "equipment")
    cards = list_cards(case, "equipment")
    case.check("the gear slots are finger cards filling the panel, every "
               "slot shown", len(slots.get("entries", [])) == 3
               and len(cards) == 3 and all(finger_tall(c) for c in cards),
               "%d cards, %s" % (len(cards), slots.get("entries")))
    case.check("and the pieces' list is not drawn over them",
               not list_cards(case, "equipment_inv"),
               str(list_cards(case, "equipment_inv")))
    case.shot("gear")
    if len(slots.get("entries", [])) != 3 or len(bought) != 2:
        return
    tap_list_entry(case, slots.get("selected", 0), "equipment")
    pieces = wait_list(case, lambda b: len(b.get("entries", [])) == 2,
                       "equipment_inv")
    names = sorted(menu_text(e) for e in pieces.get("entries", []))
    case.check("a tap on the selected slot's card opened the pieces that fit",
               names == sorted(bought), str(names))
    piece_cards = wait_cards(case, "equipment_inv")
    case.check("as finger cards in the same panel, both shown",
               len(piece_cards) == 2 and all(finger_tall(c)
                                             for c in piece_cards),
               str(piece_cards))
    case.check("in place of the slots' cards",
               not list_cards(case, "equipment"),
               str(list_cards(case, "equipment")))
    case.shot("gear-pieces")
    check_footers(case, read_menu(case), "gear pieces")
    target =[menu_text(e) for e in pieces["entries"]].index(bought[1])
    select_list_entry(case, target, "equipment_inv")
    tap_list_entry(case, target, "equipment_inv")
    equipped = wait_list(case, lambda b: any(
        menu_text(e) == bought[1] for e in b.get("entries", [])), "equipment")
    case.check("a second tap on %s equipped it, back on the slots" % bought[1],
               any(menu_text(e) == bought[1]
                   for e in equipped.get("entries", [])),
               str(equipped.get("entries")))
    case.shot("gear-equipped")
    check_footers(case, read_menu(case), "gear slot")
    case.check("the slot offers Unequip", bool(footer_word(
        read_menu(case), "unequip")), str(read_menu(case).get("desctext")))
    tap_footer_word(case, "unequip")
    off = wait_list(case, lambda b: not any(
        menu_text(e) == bought[1] for e in b.get("entries", [])), "equipment")
    case.check("Unequip took it off",
               not any(menu_text(e) == bought[1]
                       for e in off.get("entries", [])),
               str(off.get("entries")))
    tap_list_entry(case, off.get("selected", 0), "equipment")
    wait_list(case, lambda b: len(b.get("entries", [])) == 2, "equipment_inv")
    drop = footer_button(read_menu(case), "$MENU_DROP")
    case.check("the pieces offer Drop as a finger button", bool(drop),
               str(read_menu(case).get("desctext")))
    tap_button_edge(case, drop)
    dropped = wait_list(case, lambda b: len(b.get("entries", [])) == 1,
                        "equipment_inv")
    case.check("a tap on Drop (the pad's RB) dropped the selected piece",
               len(dropped.get("entries", [])) == 1,
               str(dropped.get("entries")))
    case.shot("gear-dropped")
    leave_details(case)


def wait_cards(case: Case, name: str, timeout: float = 5) -> list[dict]:
    """The list's touch cards once its draw shows some: the focus moves to
    a list a few frames after the tap that opens it."""
    deadline = time.monotonic() + timeout
    cards = list_cards(case, name)
    while time.monotonic() < deadline and not cards:
        time.sleep(0.3)
        cards = list_cards(case, name)
    return cards


def case_touch_team_ai(case: Case) -> None:
    """The ai column: a tap on a setting's card selects it, a tap on the
    selected one changes it."""
    if not start_levelled(case):
        return
    menu = open_details(case, "ai")
    (case.dir / "ai.json").write_text(json.dumps(menu, indent=1) + "\n")
    cards = [a for a in AI if button_on(menu, "label_" + a, "select")
             or button_on(menu, "label_" + a, "change")]
    case.check("the column has a card for each of the seven ai settings",
               cards == AI, str(cards))
    check_column(case, menu, "ai")
    case.shot("ai")
    target = "ai_auto_equip" if not lit(menu, "label_ai_auto_equip") \
        else "ai_heal_pickup"
    before = menu_labels(case)
    tap_named(case, "select", "label_" + target)
    wait_game_menu(case, lambda m: lit(read_menu_items(case),
                                       "label_" + target), 10)
    case.check("a tap on its card selected %s" % target,
               lit(read_menu_items(case), "label_" + target))
    case.check("and changed nothing",
               all(menu_labels(case).get(a) == before.get(a) for a in AI))
    before = menu_labels(case)
    tap_named(case, "change", "label_" + target)
    changed = wait_labels_change(case, [target], before)
    case.check("a second tap changed it in the game",
               changed.get(target) not in (None, before.get(target)),
               "%s -> %s" % (before.get(target), changed.get(target)))
    case.shot("ai-changed")
    leave_details(case)


def hero_power_slots(case: Case) -> list[str]:
    """The power slots of the hero the tabs show."""
    name = menu_labels(case).get("name")
    for n in PARTY:
        hero = menu_item(case, n).get("hero", {})
        if hero.get("display_name") == name:
            return hero.get("power_slots", [])
    return []


def slots_like_diamond(menu: dict) -> bool:
    """The slot buttons sit as their slots do in the diamond."""
    area = {b["name"]: b["area"] for b in menu.get("touch_buttons", [])
            if b.get("name") in SLOT_BUTTONS and "area" in b}
    if len(area) != len(SLOT_BUTTONS):
        return False
    left, right, bottom = (area["Left slot"], area["Right slot"],
                           area["Bottom slot"])
    return (left["right"] <= right["left"]
            and bottom["top"] <= min(left["bottom"], right["bottom"]))


def case_touch_team_assign(case: Case) -> None:
    """The skills tab: Assign (RB) takes the selected skill, the column
    becomes the diamond's three slots and Cancel, each slot button putting
    the skill in its slot; Next hero turns to the next hero."""
    if not start_levelled(case):
        return
    open_details(case, "skills")
    box = menu_list(case, "skill_list")
    ranked = next((i for i in range(len(box.get("entries", [])))
                   if skill_rank(box, i)[0] > 0), None)
    if ranked is not None and box.get("selected") != ranked:
        tap_skill_row(case, ranked)
        wait_list(case, lambda b: b.get("selected") == ranked, "skill_list")
    before = hero_power_slots(case)
    case.check("the hero's four power slots read", len(before) == 4,
               str(before))
    menu = read_menu_items(case)
    case.check("the selected skill has a rank and the column offers Assign",
               ranked is not None and bool(buttons_named(menu, "assign")))
    case.check("in place of the game's Assign prompt",
               not footer_button(read_menu(case), "$MENU_DROP"))
    if not buttons_named(menu, "assign") or len(before) != 4:
        return
    tap_named(case, "assign")
    assigning = wait_game_menu(case, lambda m: "assigning_skill" in m, 10)
    menu = read_menu_items(case)
    slots = [b.get("name") for b in menu.get("touch_buttons", [])
             if b.get("name") in (*SLOT_BUTTONS, "Cancel")]
    case.check("a tap on Assign set the game assigning the skill",
               "assigning_skill" in assigning,
               str(assigning.get("assigning_skill")))
    case.check("and the column became the three slots and Cancel",
               sorted(slots) == sorted((*SLOT_BUTTONS, "Cancel")),
               str(slots))
    case.check("laid out as the diamond's slots: left and right side by "
               "side, bottom below them", slots_like_diamond(menu))
    case.check("with the hero buttons gone while it lasts",
               not buttons_named(menu, "Next hero"))
    check_column(case, menu, "assigning")
    case.shot("skills-assigning")
    tap_named(case, "Cancel")
    cancelled = wait_game_menu(case, lambda m: "assigning_skill" not in m, 10)
    case.check("Cancel ended it unassigned",
               "assigning_skill" not in cancelled
               and hero_power_slots(case) == before,
               str(hero_power_slots(case)))
    power = ""
    for name, slot in SLOT_BUTTONS.items():
        tap_named(case, "assign")
        wait_game_menu(case, lambda m: "assigning_skill" in m, 10)
        tap_named(case, name)
        done = wait_game_menu(case, lambda m: "assigning_skill" not in m, 10)
        after = hero_power_slots(case)
        power = power or (after[slot] if len(after) == 4 else "")
        case.check("a tap on %s put the skill in the diamond's slot %d"
                   % (name, slot),
                   "assigning_skill" not in done and len(after) == 4
                   and after[slot] == power != "",
                   "%s -> %s" % (before, after))
    case.shot("skills-assigned")
    hero = menu_labels(case).get("name")
    tap_named(case, "Next hero")
    turned = wait_game_menu(case, lambda m: menu_labels(case).get(
        "name") not in (None, hero), 10)
    case.check("a tap on Next hero turned the tabs to the next hero",
               menu_labels(case).get("name") not in (None, hero)
               and turned.get("mode") == 3,
               "%s -> %s, mode %s" % (hero, menu_labels(case).get("name"),
                                      turned.get("mode")))
    case.shot("next-hero")
    tap_named(case, "Prev hero")
    wait_game_menu(case, lambda m: menu_labels(case).get("name") == hero, 10)
    case.check("and Prev hero back", menu_labels(case).get("name") == hero,
               str(menu_labels(case).get("name")))
    leave_details(case)


def case_touch_team_skill_details(case: Case) -> None:
    """The skills tab's Details: the game shows it while its prompt is held;
    on touch a tap opens it, it stays after the finger lifts, and a second
    tap closes it."""
    if not start_levelled(case):
        return
    open_details(case, "skills")
    skill = menu_text(menu_list(case, "skill_list")["entries"][
        menu_list(case, "skill_list")["selected"]])
    tap_footer_button(case, "$MENU_DETAILS")
    details = wait_game_menu(case, lambda m: m.get("mode") == 6, 10)
    case.check("a tap on Details opened the game's Details view (mode 6)",
               details.get("mode") == 6, "mode %s" % details.get("mode"))
    time.sleep(2.0)
    labels = menu_labels(case)
    (case.dir / "details.json").write_text(
        json.dumps(read_menu_items(case), indent=1) + "\n")
    case.check("it stays open with no finger down",
               read_menu(case).get("mode") == 6,
               "mode %s" % read_menu(case).get("mode"))
    case.check("on the selected skill",
               menu_text(labels.get("skill_title", "")).lower()
               == skill.lower(), "%r vs %r" % (labels.get("skill_title"),
                                               skill))
    case.shot("skill-details")
    check_footers(case, read_menu(case), "skill details")
    tap_footer_button(case, "$MENU_DETAILS")
    back = wait_game_menu(case, lambda m: m.get("mode") == 3, 10)
    case.check("a second tap closed it, back on the skill list",
               back.get("mode") == 3, "mode %s" % back.get("mode"))
    time.sleep(1.5)
    case.check("and it stays closed", read_menu(case).get("mode") == 3)
    case.shot("skill-list")
    leave_details(case)


def roster_middle(case: Case) -> str:
    """The middle roster card's hero, by the name the card shows."""
    card = menu_item(case, "roster_summary02")
    return card.get("hero", {}).get("display_name") or card.get("label", "")


def step_roster_to(case: Case, name: str) -> bool:
    """Tap the roster's lower card until `name` is the middle one."""
    for _ in range(len(roster_names(case)) + 1):
        if roster_middle(case) == name:
            return True
        before = roster_middle(case)
        tap_menu_item(case, menu_item(case, "roster_summary03"))
        wait_game_menu(case, lambda m, before=before: roster_middle(case)
                       != before, 5)
        # The cards slide for a moment after the middle one changes.
        time.sleep(1.0)
        print("  roster middle %s" % roster_middle(case))
    return roster_middle(case) == name


def case_touch_roster(case: Case) -> None:
    """CMenuTeam's roster, in the flow that has one: `loadmap <map> 0 1`
    opens the team menu with Replace, Wolverine killed first so the roster
    holds a fallen hero. A tap on the roster's lower card steps it, Replace
    revives fallen Wolverine for the game's price, then puts Sabretooth in
    the empty party slot."""
    prepare_touch_profile(case)
    case.seed_save("autosave0.save")
    case.launch({"X2_FILES": "1"})
    case.wait_control(60)
    menu = keyboard_into_gameplay(case, 300)
    case.check("the run reached gameplay through the PDA",
               menu.get("active") is False, str(menu.get("menu")))
    for command in (ADD_MONEY, ADD_MONEY, ADD_MONEY, LEVEL_UP):
        console(case, command)
    dismiss_level_up(case)
    for command in ('runscript killEntity("wolverine")',
                    "loadmap act2/jungle/jungle1 0 1"):
        console(case, command)
    party = wait_game_menu(case, lambda m: m.get("menu") == "team"
                           and m.get("mode") == 0, 120)
    time.sleep(2.0)
    items = read_menu_items(case)
    heroes = [item_in(items, n).get("label", "") for n in PARTY]
    case.check("the team menu opened on its party",
               party.get("mode") == 0, str(heroes))
    empty = next((n for n, h in zip(PARTY, heroes, strict=False) if not h), None)
    case.check("with an empty party slot", empty is not None, str(heroes))
    if empty is None:
        return
    case.shot("party")
    check_footers(case, items, "party")
    if lit_hero(items) != empty:
        tap_menu_item(case, item_in(items, empty))
        wait_game_menu(case, lambda m: lit_hero(read_menu_items(case))
                       == empty, 10)
    tap_footer_button(case, "$MENU_ACCEPT")
    roster = wait_game_menu(case, lambda m: m.get("mode") == 1, 10)
    case.check("Replace opened the game's roster", roster.get("mode") == 1,
               "mode %s" % roster.get("mode"))
    names = roster_names(case)
    case.check("it names the heroes, locked ones left out",
               "Sabretooth" in names and "Wolverine" in names
               and "Professor X" not in names, str(names))
    if "Wolverine" not in names or "Sabretooth" not in names:
        return
    time.sleep(1.0)
    case.shot("roster")
    check_footers(case, read_menu(case), "roster")
    before = roster_middle(case)
    tap_menu_item(case, menu_item(case, "roster_summary03"))
    stepped = wait_game_menu(case, lambda m: roster_middle(case) != before, 5)
    del stepped
    case.check("a tap on the lower card stepped the roster",
               roster_middle(case) != before,
               "%s -> %s" % (before, roster_middle(case)))
    case.check("Wolverine steps to the middle",
               step_roster_to(case, "Wolverine"), roster_middle(case))
    case.check("fallen", roster_hero(case, "wolverine").get("fallen") is True,
               str(roster_hero(case, "wolverine")))
    case.shot("fallen")
    cash = money(case)
    tap_footer_button(case, "$MENU_ACCEPT")
    wait_game_menu(case, lambda m: not roster_hero(case, "wolverine").get(
        "fallen", True), 20)
    print("  money %d -> %d" % (cash, money(case)))
    case.check("Replace on fallen Wolverine revived him for the game's price",
               roster_hero(case, "wolverine").get("fallen") is False
               and money(case) < cash,
               "%s, %d -> %d" % (roster_hero(case, "wolverine"), cash,
                                 money(case)))
    case.shot("revived")
    if read_menu(case).get("mode") != 1:
        tap_footer_button(case, "$MENU_ACCEPT")
        wait_game_menu(case, lambda m: m.get("mode") == 1, 10)
    case.check("Sabretooth steps to the middle",
               step_roster_to(case, "Sabretooth"), roster_middle(case))
    tap_footer_button(case, "$MENU_ACCEPT")
    wait_game_menu(case, lambda m: m.get("mode") == 0, 20)
    time.sleep(1.0)
    now = [menu_item(case, n).get("hero", {}).get("display_name", "")
           for n in PARTY]
    case.check("Replace put Sabretooth in the empty slot",
               read_menu(case).get("mode") == 0
               and now[PARTY.index(empty)] == "Sabretooth", str(now))
    case.shot("replaced")
    case.check("and the game is still running", case.alive())
