"""Live cases for the touch menu over CMenuTeam's hero detail tabs."""

from __future__ import annotations

import json
import time

from live_harness import Case
from live_menu import (
    dismiss_level_up,
    keyboard_into_gameplay,
    menu_item,
    menu_labels,
    read_menu,
    reveal_touch_row,
    tap_touch_button,
    touch_button,
    touch_menu,
    wait_game_menu,
    wait_settled_touch_menu,
)

ADD_MONEY = "runscript act1/genosha/genosha1/temp_addmoney"
# Every playable hero to level 40, with points to spend and any gear wearable.
LEVEL_UP = "runscript awardXPToPlayable(2000000)"
DETAIL_TABS = ["stats", "skills", "gear", "ai"]


def console(case: Case, command: str) -> None:
    case.http("/console?command=" + command.replace(" ", "%20"))
    time.sleep(0.5)


def start_levelled(case: Case, extra: tuple[str, ...] = ()) -> bool:
    case.prepare_profile(["boot.mode=continue", "input.touch_controls=2"])
    case.seed_save("autosave.save")
    case.launch({"X2_FILES": "1"})
    case.wait_control(60)
    menu = keyboard_into_gameplay(case, 300)
    case.check("the run reached gameplay through the PDA",
               menu.get("active") is False, str(menu.get("menu")))
    for command in (*extra, LEVEL_UP):
        console(case, command)
    dismiss_level_up(case)
    return menu.get("active") is False


def open_details(case: Case, tab: str) -> dict:
    """Open the team menu, the selected hero's details by two taps, then
    `tab` by a tap on it; the touch menu shown, or {} when it never was."""
    console(case, "openmenu team")
    party = wait_settled_touch_menu(case, "team", 20)
    case.check("the team menu opened on its party under the touch menu",
               party.get("visible") is True and read_menu(case).get("mode") == 0,
               str(read_menu(case).get("mode")))
    selected = party.get("focused_row", -1)
    hero = next((b for b in party.get("buttons", [])
                 if b["part"] == "row" and b["index"] == selected), None)
    if hero is None:
        return {}
    tap_touch_button(case, party, hero)
    stats = wait_game_menu(case, lambda m: m.get("mode") == 2 and m.get(
        "touch_menu", {}).get("tabs"), 10).get("touch_menu", {})
    tabs = [t["label"] for t in stats.get("tabs", [])]
    case.check("a tap on the selected hero opened its details under the "
               "touch menu, with the game's tabs",
               read_menu(case).get("mode") == 2 and tabs == DETAIL_TABS
               and stats.get("visible") is True, str(tabs))
    if tab == "stats" or tabs != DETAIL_TABS:
        return stats
    tap_touch_button(case, stats, touch_button(stats, "tab", tab))
    want = 2 + DETAIL_TABS.index(tab)
    opened = wait_game_menu(case, lambda m: m.get("mode") == want and any(
        t["lit"] and t["label"] == tab
        for t in m.get("touch_menu", {}).get("tabs", [])), 10)
    case.check("a tap on the %s tab opened it in the game" % tab,
               opened.get("mode") == want, "mode %s" % opened.get("mode"))
    return opened.get("touch_menu", {})


def row_value(shown: dict, label: str) -> str | None:
    for row in shown.get("rows", []):
        if row["label"].lower() == label:
            return row.get("value")
    return None


def fact(shown: dict, label: str) -> str | None:
    for entry in shown.get("facts", []):
        if entry["label"] == label:
            return entry["value"]
    return None


def focused_label(shown: dict) -> str | None:
    index = shown.get("focused_row", -1)
    rows = shown.get("rows", [])
    return rows[index]["label"].lower() if 0 <= index < len(rows) else None


def tap_row(case: Case, label: str) -> bool:
    shown, button = reveal_touch_row(case, label)
    if button is None:
        return False
    return tap_touch_button(case, shown, button)


def walk_then_press(case: Case, label: str, changed, what: str) -> dict:
    """Tap `label` once, which only walks the game's focus to it, then again,
    which presses A on it; `changed` judges the second tap's effect."""
    before = touch_menu(case)
    tap_row(case, label)
    moved = wait_game_menu(case, lambda m: focused_label(
        m.get("touch_menu", {})) == label, 10).get("touch_menu", {})
    case.check("a tap on %s moved the game's focus to it" % label,
               focused_label(moved) == label, str(focused_label(moved)))
    case.check("and changed nothing else",
               [r.get("value") for r in moved.get("rows", [])]
               == [r.get("value") for r in before.get("rows", [])]
               and moved.get("facts") == before.get("facts"),
               "%s -> %s" % (before.get("facts"), moved.get("facts")))
    tap_row(case, label)
    after = wait_game_menu(case, lambda m: changed(
        moved, m.get("touch_menu", {})), 10).get("touch_menu", {})
    case.check("a second tap %s" % what, changed(moved, after),
               "%s -> %s" % (row_value(moved, label), row_value(after, label)))
    return after


def leave_details(case: Case) -> None:
    shown = touch_menu(case)
    tap_touch_button(case, shown, touch_button(shown, "footer", "accept"))
    party = wait_game_menu(case, lambda m: m.get("mode") == 0, 10)
    case.check("the details' Accept footer returned to the party",
               party.get("mode") == 0, "mode %s" % party.get("mode"))
    case.check("and the game is still running", case.alive())


def case_touch_team_stats(case: Case) -> None:
    """The stats tab: a tap walks to a stat, a second tap adds a point."""
    if not start_levelled(case):
        return
    shown = open_details(case, "stats")
    (case.dir / "stats.json").write_text(json.dumps(shown, indent=1) + "\n")
    labels = [r["label"] for r in shown.get("rows", [])]
    case.check("its rows are the game's four stats with their values",
               labels == ["body", "focus", "strike", "speed"]
               and all(r.get("value") for r in shown.get("rows", [])),
               str(labels))
    points = fact(shown, "remaining points")
    case.check("and the points left to spend", points not in (None, "0"),
               str(points))
    case.check("and the resistances by name and value",
               any(line.startswith("Mental Resistance ")
                   for line in shown.get("detail", [])),
               str(shown.get("detail")))
    case.shot("stats")
    if labels != ["body", "focus", "strike", "speed"] or points is None:
        return
    current = focused_label(shown)
    target = "strike" if current != "strike" else "speed"
    added = walk_then_press(
        case, target,
        lambda a, b: row_value(b, target) == str(
            int(row_value(a, target) or 0) + 1)
        and fact(b, "remaining points") == str(
            int(fact(a, "remaining points") or 0) - 1),
        "added a point to it in the game")
    case.check("as the game's own item shows it",
               menu_labels(case).get(target) == row_value(added, target),
               "%r vs %r" % (menu_labels(case).get(target),
                             row_value(added, target)))
    case.shot("stats-added")
    leave_details(case)


def case_touch_team_skills(case: Case) -> None:
    """The skills tab: ranks read from the game's glyphs; a tap walks to a
    skill, a second tap adds a rank."""
    if not start_levelled(case):
        return
    shown = open_details(case, "skills")
    (case.dir / "skills.json").write_text(json.dumps(shown, indent=1) + "\n")
    rows = shown.get("rows", [])
    ranked = [r for r in rows if ", rank " in str(r.get("value"))]
    locked = [r for r in rows if "Req:" in str(r.get("value"))]
    case.check("its rows are the hero's skills, ranked or locked",
               len(rows) > 3 and ranked and locked,
               str([(r["label"], r["value"]) for r in rows]))
    points = fact(shown, "remaining points")
    case.check("and the skill points left", points not in (None, "0"),
               str(points))
    case.shot("skills")
    current = focused_label(shown)
    target = next((r["label"].lower() for r in ranked
                   if r["label"].lower() != current
                   and r["value"].split("rank ")[1].split("/")[0]
                   != r["value"].split("/")[1]), None)
    if target is None or points is None:
        return

    def rank(view: dict) -> int:
        return int(str(row_value(view, target)).split("rank ")[1].split("/")[0])

    walk_then_press(
        case, target,
        lambda a, b: row_value(b, target) is not None
        and rank(b) == rank(a) + 1
        and fact(b, "remaining points") == str(
            int(fact(a, "remaining points") or 0) - 1),
        "added a rank to it in the game")
    case.shot("skills-added")
    leave_details(case)


def case_touch_team_gear(case: Case) -> None:
    """The gear tab: a slot's A opens the pieces that fit, a piece's A equips
    it, Unequip takes it off and Drop (the pad's RB) drops one."""
    if not start_levelled(case, (ADD_MONEY, ADD_MONEY, ADD_MONEY)):
        return
    console(case, "openmenu shop")
    shop = wait_settled_touch_menu(case, "shop", 20)
    tap_touch_button(case, shop, touch_button(shop, "tab", "buy"))
    time.sleep(1.5)
    bought = []
    for _ in range(2):
        shop = touch_menu(case)
        gear = next((r["label"] for r in shop.get("rows", [])
                     if "Pack" not in r["label"] and r["label"] != "Grab Bag"
                     and r["label"] not in bought), None)
        if gear is None:
            break
        for _tap in range(2):
            tap_row(case, gear.lower())
            time.sleep(1.5)
        bought.append(gear)
    count = menu_labels(case).get("inventory_count")
    case.check("two pieces of gear bought through the touch shop",
               count == "2/20", "%s, %s" % (bought, count))
    shop = touch_menu(case)
    tap_touch_button(case, shop, touch_button(shop, "footer", "accept"))
    wait_game_menu(case, lambda m: m.get("menu") != "shop", 10)
    shown = open_details(case, "gear")
    (case.dir / "gear.json").write_text(json.dumps(shown, indent=1) + "\n")
    slots = [r["label"] for r in shown.get("rows", [])]
    case.check("its rows are the game's three gear slots, empty",
               slots == ["Nothing Equipped"] * 3, str(slots))
    case.check("and the gear count", fact(shown, "gear") == "2/20",
               str(shown.get("facts")))
    case.shot("gear")
    if len(slots) != 3 or len(bought) != 2:
        return
    tap_row(case, "nothing equipped")
    pieces = wait_game_menu(case, lambda m: [
        r["label"] for r in m.get("touch_menu", {}).get("rows", [])]
        != slots, 10).get("touch_menu", {})
    names = sorted(r["label"] for r in pieces.get("rows", []))
    case.check("a tap on the selected slot opened the pieces that fit it",
               names == sorted(bought), str(names))
    case.shot("gear-pieces")
    target = bought[1].lower()
    equipped = walk_then_press(
        case, target,
        lambda a, b: fact(b, "gear") == "1/20"
        and any(r["label"].lower() == target for r in b.get("rows", [])),
        "equipped it, back on the slots")
    slot_names = [r["label"].lower() for r in equipped.get("rows", [])]
    case.check("the first slot now names it",
               slot_names[:1] == [target], str(slot_names))
    case.shot("gear-equipped")

    shown = touch_menu(case)
    tap_touch_button(case, shown, touch_button(shown, "footer", "unequip"))
    off = wait_game_menu(case, lambda m: fact(
        m.get("touch_menu", {}), "gear") == "2/20", 10).get("touch_menu", {})
    case.check("Unequip took it off",
               fact(off, "gear") == "2/20"
               and [r["label"] for r in off.get("rows", [])] == slots,
               str(off.get("facts")))
    tap_row(case, "nothing equipped")
    wait_game_menu(case, lambda m: [
        r["label"] for r in m.get("touch_menu", {}).get("rows", [])]
        != slots, 10)
    shown = touch_menu(case)
    drop = touch_button(shown, "footer", "drop")
    case.check("the pieces offer Drop", drop is not None,
               str([b["label"] for b in shown.get("buttons", [])
                    if b["part"] == "footer"]))
    if drop is None:
        return
    tap_touch_button(case, shown, drop)
    dropped = wait_game_menu(case, lambda m: fact(
        m.get("touch_menu", {}), "gear") == "1/20", 10).get("touch_menu", {})
    case.check("a tap on Drop (the pad's RB) dropped the selected piece",
               fact(dropped, "gear") == "1/20"
               and len(dropped.get("rows", [])) == 1,
               "%s %s" % (dropped.get("facts"),
                          [r["label"] for r in dropped.get("rows", [])]))
    case.shot("gear-dropped")
    shown = touch_menu(case)
    tap_touch_button(case, shown, touch_button(shown, "footer", "accept"))
    back = wait_game_menu(case, lambda m: [
        r["label"] for r in m.get("touch_menu", {}).get("rows", [])]
        == slots, 10)
    case.check("Accept from the pieces returned to the slots",
               back.get("mode") == 4, "mode %s" % back.get("mode"))
    leave_details(case)


def case_touch_team_ai(case: Case) -> None:
    """The ai tab: a tap walks to a setting, a second tap changes it."""
    if not start_levelled(case):
        return
    shown = open_details(case, "ai")
    (case.dir / "ai.json").write_text(json.dumps(shown, indent=1) + "\n")
    labels = [r["label"] for r in shown.get("rows", [])]
    case.check("its rows are the game's seven ai settings with values",
               len(labels) == 7 and labels[0] == "ai heal when full"
               and all(r.get("value") for r in shown.get("rows", [])),
               str(labels))
    case.check("and the current setting's description",
               bool(shown.get("detail")), str(shown.get("detail")))
    case.shot("ai")
    if len(labels) != 7:
        return
    target = "ai auto-equip" if focused_label(shown) != "ai auto-equip" \
        else "ai heal when full"
    walk_then_press(
        case, target,
        lambda a, b: row_value(b, target) not in (None, row_value(a, target)),
        "changed the setting in the game")
    case.shot("ai-changed")
    leave_details(case)


def hero_power_slots(case: Case) -> list[str]:
    """The power slots of the hero the tabs show, from the party card whose
    hero has that name."""
    name = menu_labels(case).get("name")
    for n in range(1, 5):
        hero = menu_item(case, "char_summary0%d" % n).get("hero", {})
        if hero.get("display_name") == name:
            return hero.get("power_slots", [])
    return []


def footer_labels(shown: dict) -> list[str]:
    return [b["label"] for b in shown.get("buttons", [])
            if b["part"] == "footer"]


def case_touch_team_assign(case: Case) -> None:
    """The skills tab: Assign (RB) takes the selected skill, Power 2 (B) puts
    it in slot 1, Cancel clicks Back; Next hero (RT) turns to the next
    hero."""
    if not start_levelled(case):
        return
    shown = open_details(case, "skills")
    ranked = [r["label"].lower() for r in shown.get("rows", [])
              if ", rank " in str(r.get("value"))
              and not str(r.get("value")).split("rank ")[1].startswith("0/")]
    if ranked and focused_label(shown) not in ranked:
        tap_row(case, ranked[0])
        shown = wait_game_menu(case, lambda m: focused_label(
            m.get("touch_menu", {})) == ranked[0], 10).get("touch_menu", {})
    case.check("the selected skill has a rank, so the game offers Assign",
               "Assign" in footer_labels(shown), str(footer_labels(shown)))
    before = hero_power_slots(case)
    case.check("the hero's four power slots read", len(before) == 4,
               str(before))
    if "Assign" not in footer_labels(shown) or len(before) != 4:
        return

    tap_touch_button(case, shown, touch_button(shown, "footer", "assign"))
    assigning = wait_game_menu(case, lambda m: "assigning_skill" in m and
                               "Power 2" in footer_labels(
                                   m.get("touch_menu", {})), 10)
    labels = footer_labels(assigning.get("touch_menu", {}))
    case.check("a tap on Assign (RB) set the game assigning the skill, and "
               "the footers became the three slots and Cancel",
               labels == ["Power 1", "Power 2", "Power 3", "Cancel"],
               "%s %s" % (assigning.get("assigning_skill"), labels))
    case.shot("skills-assigning")
    shown = assigning.get("touch_menu", {})
    tap_touch_button(case, shown, touch_button(shown, "footer", "cancel"))
    cancelled = wait_game_menu(case, lambda m: "assigning_skill" not in m, 10)
    case.check("Cancel (a click on the game's Back) ended it unassigned",
               "assigning_skill" not in cancelled
               and hero_power_slots(case) == before,
               str(hero_power_slots(case)))

    shown = cancelled.get("touch_menu", {})
    tap_touch_button(case, shown, touch_button(shown, "footer", "assign"))
    assigning = wait_game_menu(case, lambda m: "Power 2" in footer_labels(
        m.get("touch_menu", {})), 10)
    shown = assigning.get("touch_menu", {})
    tap_touch_button(case, shown, touch_button(shown, "footer", "power 2"))
    done = wait_game_menu(case, lambda m: "assigning_skill" not in m, 10)
    after = hero_power_slots(case)
    moved = before.index(after[1]) if len(after) == 4 and after[1] in before \
        else None
    case.check("a tap on Power 2 (B) put the skill in slot 1, swapping with "
               "the slot it left",
               "assigning_skill" not in done and len(after) == 4
               and after != before and after[1] != ""
               and (moved is None or after[moved] == before[1]),
               "%s -> %s" % (before, after))
    case.shot("skills-assigned")

    shown = touch_menu(case)
    hero = menu_labels(case).get("name")
    tap_touch_button(case, shown, touch_button(shown, "footer", "next hero"))
    turned = wait_game_menu(case, lambda m: menu_labels(case).get(
        "name") not in (None, hero), 10)
    case.check("a tap on Next hero (RT) turned the tabs to the next hero",
               menu_labels(case).get("name") not in (None, hero)
               and turned.get("mode") == 3,
               "%s -> %s, mode %s" % (hero, menu_labels(case).get("name"),
                                      turned.get("mode")))
    case.shot("next-hero")
    leave_details(case)


def case_touch_team_skill_details(case: Case) -> None:
    """The skills tab's Details: a tap holds LT, which the game reads as its
    Details view of the selected skill; a second tap lets go."""
    if not start_levelled(case):
        return
    shown = open_details(case, "skills")
    skill = focused_label(shown)
    tap_touch_button(case, shown, touch_button(shown, "footer", "details"))
    details = wait_game_menu(case, lambda m: m.get("mode") == 6 and m.get(
        "touch_menu", {}).get("detail"), 10)
    lines = details.get("touch_menu", {}).get("detail", [])
    (case.dir / "details.json").write_text(json.dumps(details, indent=1) + "\n")
    labels = menu_labels(case)
    case.check("a tap on Details opened the game's Details view (mode 6)",
               details.get("mode") == 6, "mode %s" % details.get("mode"))
    case.check("the touch menu reads the selected skill's name, rank and "
               "description",
               len(lines) > 3 and lines[0].lower() == skill
               and lines[1].startswith("rank ")
               and labels.get("skill_title", "").lower() == skill
               and "$" not in " ".join(lines),
               str(lines[:4]))
    case.shot("skill-details")
    time.sleep(1.0)
    case.check("the game stays in Details while LT is held",
               read_menu(case).get("mode") == 6,
               "mode %s" % read_menu(case).get("mode"))
    shown = details.get("touch_menu", {})
    tap_touch_button(case, shown, touch_button(shown, "footer", "details"))
    back = wait_game_menu(case, lambda m: m.get("mode") == 3 and m.get(
        "touch_menu", {}).get("rows"), 10)
    case.check("a second tap let LT go, back to the skill list",
               back.get("mode") == 3
               and focused_label(back.get("touch_menu", {})) == skill,
               "mode %s" % back.get("mode"))
    case.shot("skill-list")
    leave_details(case)
