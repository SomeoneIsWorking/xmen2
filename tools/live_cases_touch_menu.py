"""Live cases for the touch menu over each retail menu class it replaces."""

from __future__ import annotations

import json
import re
import time

from live_game import live_controls
from live_harness import Case
from live_menu import (
    MAIN_MENU_LABELS,
    dismiss_level_up,
    drag_touch,
    keyboard_into_gameplay,
    keyboard_into_the_pda,
    keyboard_past_the_intro,
    menu_item,
    menu_labels,
    menu_list,
    read_menu,
    reveal_touch_row,
    tap_touch_button,
    touch_button,
    touch_menu,
    touch_row,
    wait_game_menu,
    wait_row_change,
    wait_settled_touch_menu,
    wait_touch_menu,
)

# The game's own first open of Options: the menu animates, so a pixel delta
# cannot tell whether a row was activated.
OPTIONS_PACKAGE = "menus/options.pkgb"


def case_touch_menu(case: Case) -> None:
    """The port's touch menu over the retail main and Options menus.

    Each tap is judged by the game's own state as GET /menu reads it: the menu
    the game has open, a volume bar's level, a toggle's value text. The
    negative comes first, so a run where every tap "works" cannot pass.
    """
    case.prepare_profile(["boot.mode=normal", "input.touch_controls=2"])
    case.launch({"X2_FILES": "1"})
    case.wait_control(60)
    case.check("the run reached the main menu",
               keyboard_past_the_intro(case, 300))
    main = wait_touch_menu(case, "main", 30)
    (case.dir / "main.json").write_text(json.dumps(main, indent=1) + "\n")
    case.check("the touch menu is shown over the main menu",
               main.get("visible") is True and main.get("menu") == "main",
               str({k: main.get(k) for k in ("visible", "menu")}))
    if not main.get("visible"):
        return
    print("  main rows: %s" % [row["label"] for row in main.get("rows", [])])
    case.check("it offers the main menu's rows",
               [row["label"].lower() for row in main["rows"]]
               == MAIN_MENU_LABELS)
    controls = live_controls(case)
    case.check("and the menu pad is not drawn under it",
               "menu-a" not in controls, "drawn: %s" % sorted(controls))
    time.sleep(1.0)
    case.shot("main")

    opened_before = case.log_text().count(OPTIONS_PACKAGE)
    title = {"left": 0.0, "top": 0.0, "width": main["viewport_width"],
             "height": main["viewport_height"] * 0.02}
    tap_touch_button(case, main, title)
    time.sleep(3.0)
    case.check("a tap on no button activates nothing",
               case.log_text().count(OPTIONS_PACKAGE) == opened_before
               and read_menu(case).get("menu") == "main")

    options_button = touch_button(main, "row", "options")
    case.check("the main menu has an OPTIONS button", options_button is not None)
    if options_button is None:
        return
    case.check("OPTIONS was tapped",
               tap_touch_button(case, main, options_button))
    options = wait_touch_menu(case, "options", 20)
    (case.dir / "options.json").write_text(json.dumps(options, indent=1)
                                           + "\n")
    case.check("the tap opened the game's Options menu",
               read_menu(case).get("menu") == "options"
               and options.get("visible") is True,
               "the game's menu is %r" % read_menu(case).get("menu"))
    if not options.get("visible"):
        return
    time.sleep(1.0)
    case.shot("options")

    volume = touch_row(options, "effects volume")
    step = touch_button(options, "step-left", "effects volume")
    case.check("the effects volume row has a step-left button",
               step is not None and volume.get("fill") is not None)
    if step is not None:
        before = volume.get("fill")
        tap_touch_button(case, options, step)
        after = wait_row_change(case, "effects volume", "fill", before, 10)
        case.check("its step-left lowered the game's effects volume",
                   after.get("fill") is not None and before is not None
                   and after["fill"] < before,
                   "%s -> %s" % (before, after.get("fill")))
        options = touch_menu(case)

    toggle = touch_row(options, "combat music")
    toggle_button = touch_button(options, "row", "combat music")
    case.check("the combat music row shows its value",
               toggle.get("value") in ("On", "Off") and toggle_button,
               repr(toggle.get("value")))
    if toggle_button:
        before = toggle.get("value")
        tap_touch_button(case, options, toggle_button)
        after = wait_row_change(case, "combat music", "value", before, 10)
        case.check("tapping it changed the game's value text",
                   after.get("value") in ("On", "Off")
                   and after.get("value") != before,
                   "%r -> %r" % (before, after.get("value")))
        time.sleep(1.0)
        case.shot("options-changed")

    options = touch_menu(case)
    back = touch_button(options, "footer", "back")
    case.check("Options offers the game's Back footer", back is not None,
               str([b["label"] for b in options.get("buttons", [])
                    if b["part"] == "footer"]))
    if back is None:
        return
    tap_touch_button(case, options, back)
    deadline = time.monotonic() + 20
    left = read_menu(case).get("menu")
    while time.monotonic() < deadline and left in (None, "options"):
        time.sleep(0.5)
        left = read_menu(case).get("menu")
    case.check("Back left Options", left not in (None, "options"),
               "the game's menu is now %r" % left)
    time.sleep(2.0)
    case.shot("after-back")
    case.check("and the game is still running",
               case.alive() and case.http("/status")[0] == 200)


def case_touch_team(case: Case) -> None:
    """The touch menu over CMenuTeam's party screen, opened from the PDA.

    Each tap is judged by the game's own state: which hero summary is lit, the
    class's mode, and the menu it has open.
    """
    case.prepare_profile(["boot.mode=continue", "input.touch_controls=2"])
    case.seed_save("autosave.save")
    case.launch({"X2_FILES": "1"})
    case.wait_control(60)
    pda = keyboard_into_the_pda(case, 300)
    case.check("the run reached the PDA", pda.get("menu") == "pda",
               str(pda.get("menu")))
    shown = wait_touch_menu(case, "pda", 20)
    case.check("the touch menu is shown over the PDA",
               shown.get("visible") is True)
    team_button = touch_button(shown, "row", "team management")
    case.check("the PDA offers Team Management", team_button is not None)
    if team_button is None:
        return
    case.shot("pda")
    tap_touch_button(case, shown, team_button)
    team = wait_touch_menu(case, "team", 20)
    (case.dir / "team.json").write_text(json.dumps(team, indent=1) + "\n")
    heroes = [row["label"] for row in team.get("rows", [])]
    print("  party: %s" % heroes)
    case.check("the tap opened the game's team menu on its party",
               read_menu(case).get("menu") == "team"
               and read_menu(case).get("mode") == 0
               and team.get("visible") is True, str(heroes))
    case.check("the touch menu offers one row per hero",
               len(heroes) >= 2 and all(heroes))
    if team.get("visible") is not True or len(heroes) < 2:
        return
    controls = live_controls(case)
    case.check("and the menu pad is not drawn under it",
               "menu-a" not in controls, "drawn: %s" % sorted(controls))
    case.shot("team")

    selected = team.get("focused_row", -1)
    target = 1 if selected != 1 else 0
    row = next(b for b in team["buttons"]
               if b["part"] == "row" and b["index"] == target)
    tap_touch_button(case, team, row)
    picked = wait_game_menu(
        case, lambda m: m.get("touch_menu", {}).get("focused_row") == target,
        10)
    case.check("a tap on %s selected that hero in the game" % heroes[target],
               picked.get("touch_menu", {}).get("focused_row") == target,
               "the lit hero is row %s" %
               picked.get("touch_menu", {}).get("focused_row"))
    case.shot("team-selected")

    tap_touch_button(case, team, row)
    details = wait_game_menu(case, lambda m: m.get("mode", 0) != 0, 10)
    case.check("a second tap opened that hero's details",
               details.get("menu") == "team" and details.get("mode", 0) >= 2,
               "mode %s" % details.get("mode"))
    details_view = wait_game_menu(case, lambda m: m.get("touch_menu", {}).get(
        "tabs"), 10).get("touch_menu", {})
    case.check("under the touch menu, on the game's detail tabs",
               details_view.get("visible") is True
               and [t["label"] for t in details_view.get("tabs", [])]
               == ["stats", "skills", "gear", "ai"],
               str(details_view.get("tabs")))
    case.shot("details")
    back = touch_button(details_view, "footer", "accept")
    if back is None:
        return
    tap_touch_button(case, details_view, back)
    team = wait_game_menu(
        case, lambda m: m.get("mode") == 0
        and m.get("touch_menu", {}).get("visible") is True, 10)
    case.check("the details' Accept (the game's Back) returned to the party",
               team.get("mode") == 0
               and team.get("touch_menu", {}).get("visible") is True,
               "mode %s" % team.get("mode"))
    shown = touch_menu(case)
    accept = touch_button(shown, "footer", "accept")
    case.check("the party offers the game's Accept footer", accept is not None,
               str([b["label"] for b in shown.get("buttons", [])
                    if b["part"] == "footer"]))
    if accept is None:
        return
    tap_touch_button(case, shown, accept)
    left = wait_game_menu(case, lambda m: m.get("menu") != "team", 10)
    case.check("Accept left the team menu",
               left.get("menu") != "team", "now %r" % left.get("menu"))
    case.shot("after-accept")
    case.check("and the game is still running", case.alive())


def case_touch_shop(case: Case) -> None:
    """The touch menu over CMenuShop, opened by the console from gameplay.

    The retail script temp_addmoney (setInventoryCount("MONEY", +2000)) gives
    the party money through the console's runscript. Each tap is judged by the
    game's own state: the lit tab, the list box's entries and selection, the
    money and potion counts, and the menu it has open.
    """
    case.prepare_profile(["boot.mode=continue", "input.touch_controls=2"])
    case.seed_save("autosave.save")
    case.launch({"X2_FILES": "1"})
    case.wait_control(60)
    menu = keyboard_into_gameplay(case, 300)
    case.check("the run reached gameplay through the PDA",
               menu.get("active") is False, str(menu.get("menu")))
    case.http("/console?command=runscript%20act1/genosha/genosha1/temp_addmoney")
    case.http("/console?command=openmenu%20shop")
    shop = wait_settled_touch_menu(case, "shop", 20)
    (case.dir / "shop.json").write_text(json.dumps(shop, indent=1) + "\n")
    tabs = shop.get("tabs", [])
    case.check("the touch menu is shown over the game's shop",
               shop.get("visible") is True
               and read_menu(case).get("class") == "CMenuShop",
               str(read_menu(case).get("class")))
    case.check("its tab bar is the game's three tabs",
               [t["label"] for t in tabs] == ["buy", "sell", "training"],
               str(tabs))
    case.check("and its rows are the list's entries",
               [r["label"] for r in shop.get("rows", [])]
               == menu_list(case).get("entries"),
               str([r["label"] for r in shop.get("rows", [])]))
    money = menu_labels(case).get("money_value")
    case.check("the money the script gave is shown as the game shows it",
               money == "2000" and {"label": "money", "value": "2000",
                                    "warn": False} in shop.get("facts", []),
               "money_value %r, facts %s" % (money, shop.get("facts")))
    case.check("and the selected entry's description",
               bool(shop.get("detail")), repr(shop.get("detail")))
    if shop.get("visible") is not True or len(tabs) != 3:
        return
    controls = live_controls(case)
    case.check("and the menu pad is not drawn under it",
               "menu-a" not in controls, "drawn: %s" % sorted(controls))
    case.shot("shop")

    lit = next((t["label"] for t in tabs if t["lit"]), None)
    # The game opens on buy and may step to training; a tab tap is judged on
    # whichever tab is not lit, and the purchase needs buy.
    targets = ["sell", "buy"] if lit == "buy" else ["buy"]
    for target_tab in targets:
        shown = touch_menu(case)
        tap_touch_button(case, shown, touch_button(shown, "tab", target_tab))
        switched = wait_game_menu(
            case, lambda m, t=target_tab: any(
                x["lit"] and x["label"] == t
                for x in m.get("touch_menu", {}).get("tabs", [])), 10)
        case.check("a tap on the %s tab opened it in the game" % target_tab,
                   any(t["lit"] and t["label"] == target_tab
                       for t in switched.get("touch_menu", {}).get("tabs", [])),
                   "was %s" % lit)
    case.shot("tab")

    shop = touch_menu(case)
    health = touch_button(shop, "row", "health pack")
    case.check("the buy tab offers a Health Pack", health is not None,
               str([r["label"] for r in shop.get("rows", [])]))
    if health is None:
        return
    tap_touch_button(case, shop, health)
    wait_game_menu(
        case, lambda m: m.get("touch_menu", {}).get("focused_row")
        == health["index"], 10)
    shop = touch_menu(case)
    row = shop.get("rows", [])[health["index"]]
    labels = menu_labels(case)
    cost = labels.get("item_cost_value", "")
    case.check("a tap on Health Pack selected it in the game",
               row["focused"] and menu_list(case).get("selected")
               == health["index"], "selected %s" % menu_list(case).get("selected"))
    case.check("and it shows the cost the game priced it at",
               row["value"] != "" and cost.endswith(row["value"]),
               "row %r, item_cost_value %r" % (row["value"], cost))
    case.shot("selected")

    before_money = int(labels.get("money_value", "0"))
    before_potions = int(labels.get("pot_health_value", "0"))
    price = int(row["value"] or "0")
    tap_touch_button(case, shop, touch_button(shop, "row", "health pack"))
    deadline = time.monotonic() + 10
    labels = menu_labels(case)
    while time.monotonic() < deadline \
            and labels.get("money_value") == str(before_money):
        time.sleep(0.3)
        labels = menu_labels(case)
    case.check("a tap on the selected Health Pack bought one",
               labels.get("money_value") == str(before_money - price)
               and labels.get("pot_health_value") == str(before_potions + 1),
               "money %d -> %s, health packs %d -> %s" % (
                   before_money, labels.get("money_value"), before_potions,
                   labels.get("pot_health_value")))
    shop = wait_game_menu(
        case, lambda m: {"label": "money",
                         "value": str(before_money - price), "warn": False}
        in m.get("touch_menu", {}).get("facts", []), 5).get("touch_menu", {})
    case.check("and the touch menu shows the new money",
               {"label": "money", "value": str(before_money - price),
                "warn": False} in shop.get("facts", []), str(shop.get("facts")))
    case.shot("bought")

    accept = touch_button(shop, "footer", "accept")
    case.check("the shop offers the game's Accept footer", accept is not None,
               str([b["label"] for b in shop.get("buttons", [])
                    if b["part"] == "footer"]))
    if accept is None:
        return
    tap_touch_button(case, shop, accept)
    left = wait_game_menu(case, lambda m: m.get("menu") != "shop", 10)
    case.check("Accept left the shop",
               left.get("menu") != "shop", "now %r" % left.get("menu"))
    case.shot("after-accept")
    case.check("and the game is still running", case.alive())


def case_touch_codex(case: Case) -> None:
    """The touch menu over CMenuCodex, opened by the console from gameplay.

    A tap on an entry walks the game's selection to it and accepts it, which
    loads it (0x005b1780); the Details footer opens its description. Each tap
    is judged by the game's own state: the list's selection, the codex mode
    and the loaded entry's name item.
    """
    case.prepare_profile(["boot.mode=continue", "input.touch_controls=2"])
    case.seed_save("autosave.save")
    case.launch({"X2_FILES": "1"})
    case.wait_control(60)
    menu = keyboard_into_gameplay(case, 300)
    case.check("the run reached gameplay through the PDA",
               menu.get("active") is False, str(menu.get("menu")))
    case.http("/console?command=openmenu%20codex")
    codex = wait_touch_menu(case, "codex", 20)
    (case.dir / "codex.json").write_text(json.dumps(codex, indent=1) + "\n")
    game = read_menu(case)
    case.check("the touch menu is shown over the game's codex list",
               codex.get("visible") is True
               and game.get("class") == "CMenuCodex" and game.get("mode") == 0,
               "%s mode %s" % (game.get("class"), game.get("mode")))
    entries = menu_list(case).get("entries", [])
    case.check("its rows are the list's entries",
               [r["label"] for r in codex.get("rows", [])] == entries
               and len(entries) > 4, str(entries))
    footers = [b["label"] for b in codex.get("buttons", [])
               if b["part"] == "footer"]
    case.check("and its footers are the game's Back and Details",
               footers == ["Back", "Details"], str(footers))
    if codex.get("visible") is not True or len(entries) <= 4:
        return
    case.shot("codex")

    target = entries[3]
    selected = menu_list(case).get("selected")
    case.check("the codex opened on another entry",
               selected != 3, "selected %s" % selected)
    tap_touch_button(case, codex, touch_button(codex, "row", target.lower()))
    wait_game_menu(case, lambda m: m.get("touch_menu", {}).get("focused_row")
                   == 3, 10)
    case.check("a tap on %s selected it in the game" % target,
               menu_list(case).get("selected") == 3,
               "selected %s" % menu_list(case).get("selected"))
    time.sleep(1.0)

    codex = touch_menu(case)
    tap_touch_button(case, codex, touch_button(codex, "footer", "details"))
    game = wait_game_menu(case, lambda m: m.get("mode") == 1, 10)
    reading = wait_game_menu(
        case, lambda m: m.get("touch_menu", {}).get("detail"),
        5).get("touch_menu", {})
    name = menu_labels(case).get("name", "")
    desc = menu_labels(case).get("desc", "")
    case.check("Details opened the description in the game",
               game.get("mode") == 1, "mode %s" % game.get("mode"))
    case.check("and the game loaded the tapped entry",
               name.endswith(target), "name %r" % name)
    case.check("the touch menu reads that entry's name and description",
               reading.get("title") == target and bool(reading.get("detail"))
               and reading["detail"][0] in desc,
               "title %r, first line %r" % (
                   reading.get("title"), (reading.get("detail") or [""])[0]))
    case.shot("reading")

    middle = reading["viewport_width"] * 0.5
    bottom = reading["viewport_height"] * 0.7
    drag_touch(case, reading, middle, bottom, bottom - 300.0)
    scrolled = wait_game_menu(
        case, lambda m: m.get("touch_menu", {}).get("scroll", 0) > 0,
        5).get("touch_menu", {})
    case.check("a drag scrolls the description and leaves the game's mode",
               scrolled.get("scroll", 0) > 0 and read_menu(case).get("mode") == 1,
               "scroll %s" % scrolled.get("scroll"))
    case.shot("scrolled")

    tap_touch_button(case, scrolled, touch_button(scrolled, "footer", "details"))
    game = wait_game_menu(case, lambda m: m.get("mode") == 0, 10)
    listed = wait_touch_menu(case, "codex", 5)
    case.check("Details again returned to the list on the same entry",
               game.get("mode") == 0 and listed.get("focused_row") == 3,
               "mode %s, focus %s" % (game.get("mode"), listed.get("focused_row")))

    back = touch_button(listed, "footer", "back")
    if back is None:
        case.check("the list offers Back", False)
        return
    tap_touch_button(case, listed, back)
    left = wait_game_menu(case, lambda m: m.get("menu") != "codex", 10)
    case.check("Back left the codex",
               left.get("menu") != "codex", "now %r" % left.get("menu"))
    case.shot("after-back")


def world_map_acts(case: Case) -> dict[str, bool]:
    """The game's act tabs that are unlocked, each with whether it is lit."""
    code, body = case.http("/menu?items=all")
    if code != 200:
        return {}
    acts = {}
    for item in json.loads(body).get("items", []):
        if re.fullmatch(r"option0\d_text", item["name"]) \
                and item["flags"] & 0x08:
            acts[item["label"]] = bool(item["flags"] & 0x01)
    return acts


def case_touch_worldmap(case: Case) -> None:
    """The touch menu over CMenuWorldMap, opened by the console from gameplay.

    Visiting a map with an extraction point unlocks it through its own load
    script (extractionUnlock), so the console's loadmap of savage1 and then
    sanctuary1 leaves one point open in each of acts 1 and 2. Each tap is
    judged by the game's own state: the lit act, the focused point, the menu
    it has open and the act the world map reopens on after travelling.
    """
    case.prepare_profile(["boot.mode=continue", "input.touch_controls=2"])
    case.seed_save("autosave.save")
    case.launch({"X2_FILES": "1"})
    case.wait_control(60)
    menu = keyboard_into_gameplay(case, 300)
    case.check("the run reached gameplay through the PDA",
               menu.get("active") is False, str(menu.get("menu")))
    for map_name in ("act2/savage/savage1", "act1/sanctuary/sanctuary1"):
        case.http("/console?command=loadmap%20" + map_name)
        time.sleep(5)
        menu = keyboard_into_gameplay(case, 300)
        case.check("loadmap %s reached gameplay" % map_name,
                   menu.get("active") is False, str(menu.get("menu")))
    case.http("/console?command=openmenu%20worldmap")
    shown = wait_touch_menu(case, "worldmap", 20)
    (case.dir / "worldmap.json").write_text(json.dumps(shown, indent=1) + "\n")
    case.check("the touch menu is shown over the game's world map",
               shown.get("visible") is True
               and read_menu(case).get("class") == "CMenuWorldMap",
               str(read_menu(case).get("class")))
    acts = world_map_acts(case)
    case.check("the game opened on act 1 with acts 1 and 2 unlocked",
               acts == {"act 1": True, "act 2": False}, str(acts))
    tabs = [(t["label"], t["lit"]) for t in shown.get("tabs", [])]
    case.check("its tabs are the unlocked acts with act 1 lit",
               tabs == [("act 1", True), ("act 2", False)], str(tabs))
    rows = [(r["label"], r["focused"]) for r in shown.get("rows", [])]
    case.check("its rows are act 1's unlocked point, focused",
               rows == [("Sanctuary", True)], str(rows))
    footers = [b["label"] for b in shown.get("buttons", [])
               if b["part"] == "footer"]
    case.check("its footers are one Back and go", footers == ["Back", "go"],
               str(footers))
    case.check("and the point's region and description",
               bool(shown.get("facts")) and bool(shown.get("detail")),
               "facts %s, detail %s" % (shown.get("facts"), shown.get("detail")))
    if shown.get("visible") is not True or len(tabs) != 2:
        return
    case.shot("worldmap")

    tap_touch_button(case, shown, touch_button(shown, "footer", "go"))
    time.sleep(3)
    case.check("go on the map the party is on stays in the world map",
               read_menu(case).get("menu") == "worldmap",
               str(read_menu(case).get("menu")))

    shown = touch_menu(case)
    tap_touch_button(case, shown, touch_button(shown, "tab", "act 2"))
    wait_game_menu(case, lambda m: any(
        t["lit"] and t["label"] == "act 2"
        for t in m.get("touch_menu", {}).get("tabs", [])), 10)
    acts = world_map_acts(case)
    shown = touch_menu(case)
    rows = [(r["label"], r["focused"]) for r in shown.get("rows", [])]
    case.check("a tap on act 2 opened it in the game",
               acts == {"act 1": False, "act 2": True}, str(acts))
    case.check("and the rows became act 2's point, focused",
               rows == [("Avalon", True)], str(rows))
    case.shot("act2")

    avalon = touch_button(shown, "row", "avalon")
    if avalon is None:
        return
    loading = 'front end menu "" -> "loading"'
    loads = case.log_text().count(loading)
    tap_touch_button(case, shown, avalon)
    left = wait_game_menu(case, lambda m: m.get("menu") != "worldmap", 10)
    case.check("a tap on the focused Avalon left the world map",
               left.get("menu") != "worldmap", str(left.get("menu")))
    deadline = time.monotonic() + 30
    while time.monotonic() < deadline \
            and case.log_text().count(loading) == loads:
        time.sleep(0.5)
    case.check("and the game loaded a map",
               case.log_text().count(loading) > loads)
    time.sleep(10)
    case.shot("travelled")
    case.http("/console?command=openmenu%20worldmap")
    shown = wait_touch_menu(case, "worldmap", 20)
    acts = world_map_acts(case)
    case.check("the world map reopens on act 2, the party's new act",
               acts == {"act 1": False, "act 2": True}, str(acts))
    back = touch_button(shown, "footer", "back")
    if back is None:
        case.check("the world map offers Back", False)
        return
    tap_touch_button(case, shown, back)
    left = wait_game_menu(case, lambda m: m.get("menu") != "worldmap", 10)
    case.check("Back left the world map",
               left.get("menu") != "worldmap", str(left.get("menu")))


def option_tabs(case: Case, pattern: str) -> dict[str, bool]:
    """The game's tabs whose names match `pattern`, each with whether it is
    lit; tabs without text are left out."""
    code, body = case.http("/menu?items=all")
    if code != 200:
        return {}
    return {item["label"]: bool(item["flags"] & 0x01)
            for item in json.loads(body).get("items", [])
            if re.fullmatch(pattern, item["name"]) and item["label"]}


def case_touch_stash(case: Case) -> None:
    """The touch menu over CMenuShop as the stash, opened by the console.

    The party starts with no gear, so the case buys one piece in the shop by
    touch (two temp_addmoney runs pay for it), then stores it and takes it
    back. Each tap is judged by the game's own state: the lit tab, the list's
    entries and the stash and gear counts.
    """
    case.prepare_profile(["boot.mode=continue", "input.touch_controls=2"])
    case.seed_save("autosave.save")
    case.launch({"X2_FILES": "1"})
    case.wait_control(60)
    menu = keyboard_into_gameplay(case, 300)
    case.check("the run reached gameplay through the PDA",
               menu.get("active") is False, str(menu.get("menu")))
    for _ in range(2):
        case.http("/console?command=runscript%20"
                  "act1/genosha/genosha1/temp_addmoney")
    case.http("/console?command=openmenu%20shop")
    shop = wait_settled_touch_menu(case, "shop", 20)
    tap_touch_button(case, shop, touch_button(shop, "tab", "buy"))
    wait_game_menu(case, lambda m: any(
        t["lit"] and t["label"] == "buy"
        for t in m.get("touch_menu", {}).get("tabs", [])), 10)
    shop = touch_menu(case)
    gear = next((r for r in shop.get("rows", [])
                 if "Pack" not in r["label"] and r["label"] != "Grab Bag"), None)
    case.check("the buy tab offers gear", gear is not None,
               str([r["label"] for r in shop.get("rows", [])]))
    if gear is None:
        return
    name = gear["label"]
    tap_touch_button(case, shop, touch_button(shop, "row", name.lower()))
    wait_game_menu(case, lambda m: m.get("touch_menu", {}).get("focused_row")
                   == shop["rows"].index(gear), 10)
    before = menu_labels(case)
    shop = touch_menu(case)
    tap_touch_button(case, shop, touch_button(shop, "row", name.lower()))
    deadline = time.monotonic() + 10
    labels = menu_labels(case)
    while time.monotonic() < deadline \
            and labels.get("inventory_count") == before.get("inventory_count"):
        time.sleep(0.3)
        labels = menu_labels(case)
    case.check("a tap on the selected %s bought it" % name,
               labels.get("inventory_count") == "1/20"
               and labels.get("money_value") != before.get("money_value"),
               "gear %s -> %s, money %s -> %s" % (
                   before.get("inventory_count"), labels.get("inventory_count"),
                   before.get("money_value"), labels.get("money_value")))
    shop = touch_menu(case)
    tap_touch_button(case, shop, touch_button(shop, "footer", "accept"))
    wait_game_menu(case, lambda m: m.get("menu") != "shop", 10)

    case.http("/console?command=openmenu%20stash")
    stash = wait_settled_touch_menu(case, "stash", 20)
    if option_tabs(case, r"stash_option0\d").get("stash"):
        # Opened on the stash tab and not stepped to inventory: open it.
        tap_touch_button(case, stash, touch_button(stash, "tab", "inventory"))
        wait_game_menu(case, lambda m: any(
            t["lit"] and t["label"] == "inventory"
            for t in m.get("touch_menu", {}).get("tabs", [])), 10)
        stash = wait_settled_touch_menu(case, "stash", 20)
    (case.dir / "stash.json").write_text(json.dumps(stash, indent=1) + "\n")
    case.check("the touch menu is shown over the game's stash",
               stash.get("visible") is True
               and read_menu(case).get("class") == "CMenuShop",
               str(read_menu(case).get("class")))
    tabs = [(t["label"], t["lit"]) for t in stash.get("tabs", [])]
    case.check("its tabs are stash and inventory with inventory lit",
               tabs == [("stash", False), ("inventory", True)], str(tabs))
    case.check("its rows are the party's gear",
               [r["label"] for r in stash.get("rows", [])] == [name]
               == menu_list(case).get("entries"),
               str([r["label"] for r in stash.get("rows", [])]))
    case.check("with the stash count and the gear's description",
               {"label": "stash", "value": "0/60", "warn": False}
               in stash.get("facts", []) and bool(stash.get("detail")),
               "facts %s, detail %s" % (stash.get("facts"), stash.get("detail")))
    if stash.get("visible") is not True or not stash.get("rows"):
        return
    case.shot("stash")

    tap_touch_button(case, stash, touch_button(stash, "row", name.lower()))
    stored = wait_game_menu(
        case, lambda m: not m.get("touch_menu", {}).get("rows"), 10)
    labels = menu_labels(case)
    case.check("a tap on the selected %s stored it" % name,
               labels.get("inventory_count") == "1/60"
               and not menu_list(case).get("entries"),
               "stash %s, inventory %s" % (labels.get("inventory_count"),
                                           menu_list(case).get("entries")))
    case.check("and the touch menu keeps the tabs over the empty list",
               len(stored.get("touch_menu", {}).get("tabs", [])) == 2,
               str(stored.get("touch_menu", {}).get("tabs")))
    case.shot("stored")

    stash = touch_menu(case)
    tap_touch_button(case, stash, touch_button(stash, "tab", "stash"))
    wait_game_menu(case, lambda m: [r["label"] for r in m.get(
        "touch_menu", {}).get("rows", [])] == [name], 10)
    stash = touch_menu(case)
    case.check("a tap on the stash tab opened it in the game",
               option_tabs(case, r"stash_option0\d")
               == {"stash": True, "inventory": False}
               and [r["label"] for r in stash.get("rows", [])] == [name],
               "%s, rows %s" % (option_tabs(case, r"stash_option0\d"),
                                [r["label"] for r in stash.get("rows", [])]))
    case.shot("stash-tab")

    tap_touch_button(case, stash, touch_button(stash, "row", name.lower()))
    wait_game_menu(case, lambda m: not m.get("touch_menu", {}).get("rows"),
                   10)
    labels = menu_labels(case)
    case.check("a tap on it took it back to the inventory",
               labels.get("inventory_count") == "1/20",
               "gear %s" % labels.get("inventory_count"))
    stash = touch_menu(case)
    accept = touch_button(stash, "footer", "accept")
    if accept is None:
        case.check("the stash offers Accept", False)
        return
    tap_touch_button(case, stash, accept)
    left = wait_game_menu(case, lambda m: m.get("menu") != "stash", 10)
    case.check("Accept left the stash",
               left.get("menu") != "stash", str(left.get("menu")))


def case_touch_review(case: Case) -> None:
    """The touch menu over CMenuReviewPaths, opened by the console.

    It opens on the stats tab. A tap on the cinematics tab opens it in the
    game, and two taps on Credits select it and press A, which plays the
    credits; Esc returns to the review and Back leaves it.
    """
    case.prepare_profile(["boot.mode=continue", "input.touch_controls=2"])
    case.seed_save("autosave.save")
    case.launch({"X2_FILES": "1"})
    case.wait_control(60)
    menu = keyboard_into_gameplay(case, 300)
    case.check("the run reached gameplay through the PDA",
               menu.get("active") is False, str(menu.get("menu")))
    case.http("/console?command=openmenu%20review")
    review = wait_settled_touch_menu(case, "review", 20)
    (case.dir / "review.json").write_text(json.dumps(review, indent=1) + "\n")
    case.check("the touch menu is shown over the game's review",
               review.get("visible") is True
               and read_menu(case).get("class") == "CMenuReviewPaths",
               str(read_menu(case).get("class")))
    tabs = [(t["label"], t["lit"]) for t in review.get("tabs", [])]
    case.check("its tabs are the game's five with stats lit",
               tabs == [("screens", False), ("cinematics", False),
                        ("comics", False), ("concepts", False),
                        ("stats", True)], str(tabs))
    rows = review.get("rows", [])
    game = menu_list(case)
    case.check("its rows are the stats entries with their counts",
               len(rows) == len(game.get("entries", [])) > 0
               and [r["value"] for r in rows] == game.get("values")
               and {"label": "Comic Books", "value": "0 of 3"}.items()
               <= rows[1].items(),
               str([(r["label"], r["value"]) for r in rows[:3]]))
    if review.get("visible") is not True or not rows:
        return
    case.shot("stats")

    tap_touch_button(case, review, touch_button(review, "tab", "cinematics"))
    review = wait_game_menu(case, lambda m: touch_row(
        m.get("touch_menu", {}), "credits") != {}, 10).get("touch_menu", {})
    case.check("a tap on the cinematics tab opened it in the game",
               option_tabs(case, r"option0\d_text").get("cinematics") is True
               and touch_row(review, "credits") != {},
               "%s, rows %s" % (option_tabs(case, r"option0\d_text"),
                                [r["label"] for r in review.get("rows", [])]))
    case.shot("cinematics")

    if not touch_row(review, "credits").get("focused"):
        tap_touch_button(case, review, touch_button(review, "row", "credits"))
        review = wait_game_menu(case, lambda m: touch_row(
            m.get("touch_menu", {}), "credits").get("focused"),
            10).get("touch_menu", {})
    tap_touch_button(case, review, touch_button(review, "row", "credits"))
    credits = wait_game_menu(case, lambda m: m.get("menu") == "credits", 10)
    case.check("a tap on the selected Credits played them",
               credits.get("class") == "CMenuCredits"
               and credits.get("touch_menu", {}).get("visible") is False,
               "%s %s" % (credits.get("menu"), credits.get("class")))
    case.shot("credits")

    case.http("/key?name=Escape&hold=0.2")
    review = wait_touch_menu(case, "review", 10)
    case.check("Esc returned to the review under the touch menu",
               review.get("visible") is True, str(read_menu(case).get("menu")))
    back = touch_button(review, "footer", "back")
    if back is None:
        case.check("the review offers Back", False)
        return
    tap_touch_button(case, review, back)
    left = wait_game_menu(case, lambda m: m.get("menu") != "review", 10)
    case.check("Back left the review",
               left.get("menu") != "review", str(left.get("menu")))


def case_touch_region(case: Case) -> None:
    """The touch menu over CMenuRegion, opened by the console.

    With no online service the game's region list is empty; the touch menu
    shows the title and footers over it. A tap on Select opens the campaign
    lobby as the game's own A does, and Back on a reopened region leaves it.
    """
    case.prepare_profile(["boot.mode=continue", "input.touch_controls=2"])
    case.seed_save("autosave.save")
    case.launch({"X2_FILES": "1"})
    case.wait_control(60)
    menu = keyboard_into_gameplay(case, 300)
    case.check("the run reached gameplay through the PDA",
               menu.get("active") is False, str(menu.get("menu")))
    case.http("/console?command=openmenu%20region")
    region = wait_settled_touch_menu(case, "region", 20)
    (case.dir / "region.json").write_text(json.dumps(region, indent=1) + "\n")
    case.check("the touch menu is shown over the game's region list",
               region.get("visible") is True
               and read_menu(case).get("class") == "CMenuRegion",
               str(read_menu(case).get("class")))
    case.check("titled by the game's text_title",
               region.get("title") == "Region Menu", str(region.get("title")))
    rows = [r["label"] for r in region.get("rows", [])]
    game = menu_list(case, "text_list")
    case.check("its rows are the game's regions",
               "entries" in game and len(rows) == len(game["entries"]),
               "rows %s, game %s" % (rows, game.get("entries")))
    footers = [b["label"] for b in region.get("buttons", [])
               if b["part"] == "footer"]
    case.check("with the game's Back, Refresh and Select footers",
               footers == ["Back", "Refresh", "Select"], str(footers))
    if region.get("visible") is not True:
        return
    case.shot("region")

    tap_touch_button(case, region, touch_button(region, "footer", "select"))
    lobby = wait_game_menu(case, lambda m: m.get("menu") == "campaign_lobby",
                           10)
    case.check("a tap on Select opened the campaign lobby",
               lobby.get("class") == "CMenuCampaignLobby",
               "%s %s" % (lobby.get("menu"), lobby.get("class")))
    case.shot("lobby")

    case.http("/console?command=openmenu%20region")
    region = wait_touch_menu(case, "region", 20)
    back = touch_button(region, "footer", "back")
    if back is None:
        case.check("the region offers Back", False)
        return
    tap_touch_button(case, region, back)
    left = wait_game_menu(case, lambda m: m.get("menu") != "region", 10)
    case.check("Back left the region",
               left.get("menu") != "region", str(left.get("menu")))


def touch_rows(case: Case) -> list[str]:
    return [r["label"] for r in touch_menu(case).get("rows", [])]


def case_touch_danger_room(case: Case) -> None:
    """The touch menu over CMenuDangerRoom, opened by the console.

    A tap on the selected grade opens its courses, a tap on the Status tab
    opens it in the game, Back returns to the grades, and a tap on the
    selected course opens the team menu to choose who runs it.
    """
    case.prepare_profile(["boot.mode=continue", "input.touch_controls=2"])
    case.seed_save("autosave.save")
    case.launch({"X2_FILES": "1"})
    case.wait_control(60)
    menu = keyboard_into_gameplay(case, 300)
    case.check("the run reached gameplay through the PDA",
               menu.get("active") is False, str(menu.get("menu")))
    case.http("/console?command=openmenu%20danger_room")
    room = wait_settled_touch_menu(case, "danger_room", 20)
    (case.dir / "grades.json").write_text(json.dumps(room, indent=1) + "\n")
    grades = [r["label"] for r in room.get("rows", [])]
    case.check("the touch menu is shown over the game's danger room",
               room.get("visible") is True
               and read_menu(case).get("class") == "CMenuDangerRoom",
               str(read_menu(case).get("class")))
    case.check("its rows are the game's grades with no tabs",
               grades == menu_list(case).get("entries")
               and grades[:1] == ["Freshman"] and not room.get("tabs"),
               "%s, tabs %s" % (grades, room.get("tabs")))
    case.check("with the game's description and Back and Select",
               room.get("detail", [])[:1] == ["TRAINING MODE"]
               and [b["label"] for b in room.get("buttons", [])
                    if b["part"] == "footer"] == ["Back", "Select"],
               "detail %s" % room.get("detail"))
    if room.get("visible") is not True or not grades:
        return
    case.shot("grades")

    tap_touch_button(case, room, touch_button(room, "row", "freshman"))
    room = wait_game_menu(case, lambda m: bool(m.get("touch_menu", {})
                                                .get("tabs")),
                          10).get("touch_menu", {})
    courses = [r["label"] for r in room.get("rows", [])]
    case.check("a tap on the selected Freshman opened its courses",
               courses == menu_list(case).get("entries")
               and courses[:1] == ["Setting 101 - Hidden Goods"]
               and option_tabs(case, r"option_text\d")
               == {"Overview": True, "Status": False},
               "%s, %s" % (courses, option_tabs(case, r"option_text\d")))
    case.shot("courses")

    tap_touch_button(case, room, touch_button(room, "tab", "status"))
    room = wait_game_menu(case, lambda m: any(
        t["lit"] and t["label"] == "Status"
        for t in m.get("touch_menu", {}).get("tabs", [])),
        10).get("touch_menu", {})
    case.check("a tap on Status opened it in the game",
               option_tabs(case, r"option_text\d")
               == {"Overview": False, "Status": True}
               and room.get("detail", [])[:1] == ["Status: Incomplete"],
               "%s, detail %s" % (option_tabs(case, r"option_text\d"),
                                  room.get("detail")))
    case.shot("status")

    tap_touch_button(case, room, touch_button(room, "footer", "back"))
    wait_game_menu(case, lambda m: [r["label"] for r in m.get(
        "touch_menu", {}).get("rows", [])] == grades, 10)
    case.check("Back returned to the grades", touch_rows(case) == grades,
               str(touch_rows(case)))

    room = touch_menu(case)
    tap_touch_button(case, room, touch_button(room, "row", "freshman"))
    wait_game_menu(case, lambda m: bool(m.get("touch_menu", {}).get("tabs")),
                   10)
    room = touch_menu(case)
    tap_touch_button(case, room,
                     touch_button(room, "row", "setting 101 - hidden goods"))
    team = wait_game_menu(case, lambda m: m.get("menu") == "team", 10)
    case.check("a tap on the selected course opened the team menu",
               team.get("class") == "CMenuTeam",
               "%s %s" % (team.get("menu"), team.get("class")))
    case.shot("team")


def roster_names(case: Case) -> list[str]:
    """The heroes the roster's cards would name, as the model reads them."""
    heroes = menu_item(case, "roster_portrait01").get("heroes", [])
    masks = menu_item(case, "roster_summary02").get("masks_locked", True)
    return [h["display_name"] for h in heroes
            if h and (h["unlocked"] or not masks)]


def roster_hero(case: Case, internal: str) -> dict:
    for hero in menu_item(case, "roster_portrait01").get("heroes", []):
        if hero and hero["name"].lower() == internal:
            return hero
    return {}


def money(case: Case) -> int:
    return int(menu_item(case, "money_value").get("label", "0") or 0)


def case_touch_roster(case: Case) -> None:
    """The touch menu over CMenuTeam's roster, in the flow that has one.

    `loadmap <map> 0 1` opens the team menu with Replace; the heroes are
    levelled past the cost floor and Wolverine is killed first so the roster
    holds a fallen hero. A tap on him walks the carousel
    with the menu pad and A revives him for the game's price; a tap on
    Sabretooth then puts him in the empty party slot.
    """
    case.prepare_profile(["boot.mode=continue", "input.touch_controls=2"])
    case.seed_save("autosave.save")
    case.launch({"X2_FILES": "1"})
    case.wait_control(60)
    menu = keyboard_into_gameplay(case, 300)
    case.check("the run reached gameplay through the PDA",
               menu.get("active") is False, str(menu.get("menu")))
    for command in ("runscript act1/genosha/genosha1/temp_addmoney",
                    "runscript act1/genosha/genosha1/temp_addmoney",
                    "runscript act1/genosha/genosha1/temp_addmoney",
                    "runscript awardXPToPlayable(2000000)"):
        case.http("/console?command=" + command.replace(" ", "%20"))
        time.sleep(1)
    dismiss_level_up(case)
    for command in ('runscript killEntity("wolverine")',
                    "loadmap act2/jungle/jungle1 0 1"):
        case.http("/console?command=" + command.replace(" ", "%20"))
        time.sleep(1)
    party = wait_settled_touch_menu(case, "team", 120)
    rows = [r["label"] for r in party.get("rows", [])]
    case.check("the team menu opened on its party under the touch menu",
               read_menu(case).get("mode") == 0
               and party.get("visible") is True, str(rows))
    case.check("the empty party slot is named as one", "Empty slot" in rows,
               str(rows))
    if "Empty slot" not in rows:
        return
    case.shot("party")
    empty = rows.index("Empty slot")
    if party.get("focused_row") != empty:
        tap_touch_button(case, party, touch_button(party, "row", "empty slot"))
        party = wait_game_menu(case, lambda m: m.get("touch_menu", {}).get(
            "focused_row") == empty, 10).get("touch_menu", {})
    tap_touch_button(case, party, touch_button(party, "footer", "replace"))
    roster = wait_game_menu(case, lambda m: m.get("mode") == 1 and m.get(
        "touch_menu", {}).get("visible") is True, 10).get("touch_menu", {})
    (case.dir / "roster.json").write_text(json.dumps(roster, indent=1) + "\n")
    names = [r["label"] for r in roster.get("rows", [])]
    values = {r["label"]: r.get("value") for r in roster.get("rows", [])}
    case.check("Replace opened the game's roster under the touch menu",
               read_menu(case).get("mode") == 1
               and roster.get("visible") is True, str(names))
    case.check("its rows are the heroes the cards name, locked ones left out",
               names == roster_names(case) and "Sabretooth" in names
               and "Jean Grey" in names and "Professor X" not in names,
               str(names))
    case.check("and fallen Wolverine says so beside his level and cost",
               str(values.get("Wolverine", "")).startswith("Fallen, level"),
               str(values.get("Wolverine")))
    if "Wolverine" not in names or "Sabretooth" not in names:
        return
    case.shot("roster")

    before = money(case)
    roster, wolverine = reveal_touch_row(case, "wolverine")
    case.check("Wolverine's row scrolls into reach", wolverine is not None)
    if wolverine is None:
        return
    case.shot("fallen")
    tap_touch_button(case, roster, wolverine)
    wait_game_menu(case, lambda m: not roster_hero(case, "wolverine").get(
        "fallen", True), 20)
    after = money(case)
    print("  money %d -> %d" % (before, after))
    case.check("a tap on Wolverine revived him in the game",
               roster_hero(case, "wolverine").get("fallen") is False,
               str(roster_hero(case, "wolverine")))
    shown_cost = re.search(r"revive (\d+)$",
                           str(values.get("Wolverine", "")))
    case.check("and the game took the revive cost the row showed",
               shown_cost is not None
               and before - after == int(shown_cost.group(1)),
               "%d -> %d, row %r" % (before, after, values.get("Wolverine")))
    case.shot("revived")

    roster, sabretooth = reveal_touch_row(case, "sabretooth")
    if sabretooth is None:
        return
    tap_touch_button(case, roster, sabretooth)
    party = wait_game_menu(case, lambda m: m.get("mode") == 0 and m.get(
        "touch_menu", {}).get("visible") is True, 20).get("touch_menu", {})
    rows = [r["label"] for r in party.get("rows", [])]
    case.check("a tap on Sabretooth put him in the empty slot",
               read_menu(case).get("mode") == 0 and len(rows) > empty
               and rows[empty] == "Sabretooth", str(rows))
    case.shot("replaced")
    case.check("and the game is still running", case.alive())
