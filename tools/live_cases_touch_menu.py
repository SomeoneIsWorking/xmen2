"""Live cases for a finger on the retail menus, drawn natively or under the
touch menu over each class it still replaces."""

from __future__ import annotations

import json
import re
import time

from live_harness import Case
from live_menu import (
    drag_between,
    footer_button,
    item_button,
    keyboard_into_gameplay,
    list_cards,
    list_entry,
    menu_footer,
    menu_item,
    menu_items,
    menu_labels,
    menu_list,
    menu_text,
    option_tabs,
    read_menu,
    read_menu_items,
    reveal_list_entry,
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


def into_gameplay(case: Case, money_runs: int = 0) -> bool:
    """A Continue boot of autosave0 into gameplay, with `money_runs` runs of
    the retail temp_addmoney (setInventoryCount("MONEY", +2000))."""
    case.prepare_profile(["boot.mode=continue", "input.touch_controls=2"])
    case.seed_save("autosave0.save")
    case.launch({"X2_FILES": "1"})
    case.wait_control(60)
    menu = keyboard_into_gameplay(case, 300)
    case.check("the run reached gameplay through the PDA",
               menu.get("active") is False, str(menu.get("menu")))
    for _ in range(money_runs):
        case.http("/console?command=runscript%20"
                  "act1/genosha/genosha1/temp_addmoney")
    return menu.get("active") is False


def check_native_list(case: Case, menu_class: str, name: str = "list") -> bool:
    """The game's own menu of `menu_class` is up with no touch menu over it
    and its list box drawn as touch cards; whether it is."""
    menu = read_menu(case)
    case.check("the game's %s is up" % menu_class,
               menu.get("class") == menu_class, str(menu.get("class")))
    box = menu_list(case, name)
    cards = list_cards(case, name)
    entries = len(box.get("entries", []))
    case.check("its list box is drawn as touch cards",
               bool(cards) == (entries > 0)
               and all(0 <= c["entry"] < entries and "tap" in c
                       for c in cards),
               "%d card(s) over %d entries" % (len(cards), entries))
    return menu.get("class") == menu_class


def case_touch_shop(case: Case) -> None:
    """CMenuShop drawn natively, opened by the console from gameplay.

    Each tap is judged by the game's own state: the lit tab, the list box's
    entries and selection, the money and potion counts, and the menu it has
    open.
    """
    if not into_gameplay(case, money_runs=1):
        return
    case.http("/console?command=openmenu%20shop")
    items = wait_settled_menu(case, "shop", 20)
    (case.dir / "shop.json").write_text(json.dumps(items, indent=1) + "\n")
    if not check_native_list(case, "CMenuShop"):
        return
    tabs = option_tabs(case, r"shop_option0\d")
    case.check("its tabs are the game's three",
               list(tabs) == ["buy", "sell", "training"], str(tabs))
    labels = menu_labels(case)
    case.check("the money the script gave is shown",
               labels.get("money_value") == "2000",
               "money_value %r" % labels.get("money_value"))
    case.check("and the selected entry's description",
               bool(menu_text(labels.get("item_desc", ""))),
               repr(labels.get("item_desc")))
    case.shot("shop")

    lit = next((label for label, on in tabs.items() if on), None)
    # The game opens on buy and may step to training; a tab tap is judged on
    # whichever tab is not lit, and the purchase needs buy.
    for target_tab in (["sell", "buy"] if lit == "buy" else ["buy"]):
        tap_tab(case, r"shop_option0\d", target_tab)
        deadline = time.monotonic() + 10
        while time.monotonic() < deadline and not option_tabs(
                case, r"shop_option0\d").get(target_tab):
            time.sleep(0.3)
        case.check("a tap on the %s tab opened it in the game" % target_tab,
                   option_tabs(case, r"shop_option0\d").get(target_tab)
                   is True, "was %s" % lit)
    case.shot("tab")

    box = wait_list(case, lambda b: list_entry(b, "health pack") >= 0)
    health = list_entry(box, "health pack")
    case.check("the buy tab offers a Health Pack", health >= 0,
               str(box.get("entries")))
    if health < 0:
        return
    card = reveal_list_entry(case, health)
    case.check("Health Pack has a touch card", card is not None,
               str(list_cards(case)))
    after = menu_list(case)
    if after.get("top") != box.get("top"):
        case.check("a drag scrolled the cards and left the selection",
                   after.get("selected") == box.get("selected"),
                   "selected %s -> %s" % (box.get("selected"),
                                          after.get("selected")))
    if card is None:
        return
    tap_menu_item(case, card)
    box = wait_list(case, lambda b: b.get("selected") == health)
    # The selection update blanks the price for a frame before writing it.
    labels = wait_labels(case, lambda now: bool(now.get("item_cost_value")))
    cost = menu_text(labels.get("item_cost_value", ""))
    case.check("a tap on Health Pack selected it in the game",
               box.get("selected") == health, "selected %s" % box.get("selected"))
    case.check("and it shows the cost the game priced it at",
               cost.isdigit() and int(cost) > 0,
               "item_cost_value %r" % labels.get("item_cost_value"))
    case.shot("selected")

    before_money = int(labels.get("money_value", "0"))
    before_potions = int(labels.get("pot_health_value", "0"))
    price = int(cost) if cost.isdigit() else 0
    tap_list_entry(case, health)
    labels = wait_labels(
        case, lambda now: now.get("money_value") != str(before_money))
    case.check("a tap on the selected Health Pack bought one",
               labels.get("money_value") == str(before_money - price)
               and labels.get("pot_health_value") == str(before_potions + 1),
               "money %d -> %s, health packs %d -> %s" % (
                   before_money, labels.get("money_value"), before_potions,
                   labels.get("pot_health_value")))
    case.shot("bought")

    box = menu_list(case)
    cards = list_cards(case)
    if len(cards) >= 2:
        drag_between(case, cards[-1]["tap"], cards[0]["tap"])
    dragged = wait_list(case, lambda b: b.get("top") != box.get("top"))
    case.check("a drag up scrolled the cards and left the selection",
               dragged.get("top", 0) > box.get("top", 0)
               and dragged.get("selected") == box.get("selected")
               and list_cards(case)[:1] != cards[:1],
               "top %s -> %s, selected %s -> %s" % (
                   box.get("top"), dragged.get("top"), box.get("selected"),
                   dragged.get("selected")))
    case.shot("dragged")

    menu = read_menu(case)
    case.check("the shop's Buy and Accept are finger buttons",
               bool(footer_button(menu, "$MENU_ACCEPT"))
               and bool(footer_button(menu, "$MENU_OK")),
               str(menu.get("touch_buttons")))
    labels = menu_labels(case)
    before_money = int(labels.get("money_value", "0"))
    before_potions = int(labels.get("pot_health_value", "0"))
    tap_footer_button(case, "$MENU_ACCEPT")
    labels = wait_labels(
        case, lambda now: now.get("money_value") != str(before_money))
    case.check("a tap on the Buy button bought another Health Pack",
               labels.get("money_value") == str(before_money - price)
               and labels.get("pot_health_value") == str(before_potions + 1),
               "money %d -> %s, health packs %d -> %s" % (
                   before_money, labels.get("money_value"), before_potions,
                   labels.get("pot_health_value")))
    case.shot("buttons")

    case.check("the shop offers the game's Accept button",
               tap_footer_button(case, "$MENU_OK"),
               str(read_menu(case).get("touch_buttons")))
    left = wait_game_menu(case, lambda m: m.get("menu") != "shop", 10)
    case.check("Accept left the shop",
               left.get("menu") != "shop", "now %r" % left.get("menu"))
    case.shot("after-accept")
    case.check("and the game is still running", case.alive())


def case_touch_codex(case: Case) -> None:
    """CMenuCodex drawn natively, opened by the console from gameplay.

    One tap on an entry selects it and accepts it, which loads it
    (0x005b1780); the Details footer opens its description. Each tap is
    judged by the game's own state: the list's selection, the codex mode and
    the loaded entry's name item.
    """
    if not into_gameplay(case):
        return
    case.http("/console?command=openmenu%20codex")
    wait_game_menu(case, lambda m: m.get("menu") == "codex", 20)
    box = wait_list(case, lambda b: len(b.get("entries", [])) > 4
                    and bool(list_cards(case)), timeout=20)
    (case.dir / "codex.json").write_text(
        json.dumps(menu_items(case), indent=1) + "\n")
    if not check_native_list(case, "CMenuCodex"):
        return
    entries = box.get("entries", [])
    case.check("it opened on its list", read_menu(case).get("mode") == 0,
               "mode %s" % read_menu(case).get("mode"))
    menu = read_menu(case)
    case.check("with the game's Back and Details footers",
               bool(menu_footer(menu, "$MENU_BACK"))
               and bool(menu_footer(menu, "$MENU_DETAILS")),
               str([t["label"] for t in menu.get("texts", [])]))
    if len(entries) <= 4:
        case.check("the codex lists more than four entries", False,
                   str(entries))
        return
    case.shot("codex")

    target = menu_text(entries[3])
    case.check("the codex opened on another entry",
               box.get("selected") != 3, "selected %s" % box.get("selected"))
    tap_list_entry(case, 3)
    box = wait_list(case, lambda b: b.get("selected") == 3)
    case.check("one tap on %s selected it in the game" % target,
               box.get("selected") == 3, "selected %s" % box.get("selected"))
    # The tap's A loads the entry (0x005b1780) into the name item.
    labels = wait_labels(case, lambda now: menu_text(
        now.get("name", "")).endswith(target))
    case.check("and loaded it", menu_text(labels.get("name", "")).endswith(
        target), "name %r" % labels.get("name"))
    case.shot("selected")

    case.check("its footers are finger buttons",
               bool(footer_button(menu, "$MENU_BACK"))
               and bool(footer_button(read_menu(case), "$MENU_DETAILS")),
               str(read_menu(case).get("touch_buttons")))
    tap_footer_button(case, "$MENU_DETAILS")
    game = wait_game_menu(case, lambda m: m.get("mode") == 1, 10)
    labels = menu_labels(case)
    case.check("Details opened the description in the game",
               game.get("mode") == 1, "mode %s" % game.get("mode"))
    case.check("of the tapped entry",
               menu_text(labels.get("name", "")).endswith(target)
               and bool(menu_text(labels.get("desc", ""))),
               "name %r, desc %r" % (labels.get("name"),
                                     labels.get("desc", "")[:60]))
    case.shot("reading")

    tap_footer_button(case, "$MENU_DETAILS")
    game = wait_game_menu(case, lambda m: m.get("mode") == 0, 10)
    case.check("Details again returned to the list on the same entry",
               game.get("mode") == 0 and menu_list(case).get("selected") == 3,
               "mode %s, selected %s" % (game.get("mode"),
                                         menu_list(case).get("selected")))

    case.check("the list offers Back", tap_footer_button(case, "$MENU_BACK"))
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


def world_map_points(case: Case) -> dict[str, bool]:
    """The game's unlocked point rows, each with whether it is focused."""
    return {item["label"]: bool(item.get("focused"))
            for item in menu_items(case)
            if re.fullmatch(r"list01_0\d", item["name"]) and item["flags"] & 0x08}


def tap_world_map_point(case: Case, label: str) -> bool:
    """Tap the finger button over the point row that reads `label`."""
    menu = read_menu_items(case)
    point = next((item for item in menu.get("items", [])
                  if re.fullmatch(r"list01_0\d", item["name"])
                  and item["label"] == label), {})
    return tap_button_edge(case, item_button(menu, point))


def case_touch_worldmap(case: Case) -> None:
    """CMenuWorldMap on touch: the game's own screen, its unlocked acts and
    points given finger buttons, opened by the console from gameplay.

    Visiting a map with an extraction point unlocks it through its own load
    script (extractionUnlock), so the console's loadmap of savage1 and then
    sanctuary1 leaves one point open in each of acts 1 and 2. Each tap lands
    at the edge of its finger button, outside the game's own box, and is
    judged by the game's state: the lit act, the focused point, the menu it
    has open and the act the world map reopens on after travelling.
    """
    case.prepare_profile(["boot.mode=continue", "input.touch_controls=2"])
    case.seed_save("autosave0.save")
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
    shown = wait_game_menu(case, lambda m: m.get("menu") == "worldmap", 20)
    case.check("the world map is the game's own screen",
               shown.get("class") == "CMenuWorldMap",
               str(shown.get("class")))
    menu = read_menu_items(case)
    (case.dir / "worldmap.json").write_text(json.dumps(menu, indent=1) + "\n")
    acts = world_map_acts(case)
    case.check("the game opened on act 1 with acts 1 and 2 unlocked",
               acts == {"act 1": True, "act 2": False}, str(acts))
    tabs = [item for item in menu.get("items", [])
            if re.fullmatch(r"option0\d_text", item["name"])]
    buttons = [item["label"] for item in tabs if item_button(menu, item)]
    case.check("only the unlocked acts have finger buttons",
               buttons == ["act 1", "act 2"], str(buttons))
    points = world_map_points(case)
    case.check("act 1's unlocked point is focused",
               points == {"Sanctuary": True}, str(points))
    case.check("Back and go are finger buttons",
               bool(footer_button(menu, "$MENU_BACK"))
               and bool(footer_button(menu, "$MENU_ACCEPT")))
    case.shot("worldmap")

    tap_footer_button(case, "$MENU_ACCEPT")
    time.sleep(3)
    case.check("go on the map the party is on stays in the world map",
               read_menu(case).get("menu") == "worldmap",
               str(read_menu(case).get("menu")))

    tap_tab(case, r"option0\d_text", "act 2")
    acts = wait_world_map_acts(case, {"act 1": False, "act 2": True})
    case.check("a tap on act 2 opened it in the game",
               acts == {"act 1": False, "act 2": True}, str(acts))
    points = world_map_points(case)
    case.check("and act 2's point is focused",
               points == {"Avalon": True}, str(points))
    case.shot("act2")

    loading = 'front end menu "" -> "loading"'
    loads = case.log_text().count(loading)
    tap_world_map_point(case, "Avalon")
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
    wait_game_menu(case, lambda m: m.get("menu") == "worldmap", 20)
    acts = wait_world_map_acts(case, {"act 1": False, "act 2": True})
    case.check("the world map reopens on act 2, the party's new act",
               acts == {"act 1": False, "act 2": True}, str(acts))
    tap_footer_button(case, "$MENU_BACK")
    left = wait_game_menu(case, lambda m: m.get("menu") != "worldmap", 10)
    case.check("Back left the world map",
               left.get("menu") != "worldmap", str(left.get("menu")))


def wait_world_map_acts(case: Case, want: dict[str, bool],
                        timeout: float = 10) -> dict[str, bool]:
    """The world map's unlocked acts once they read `want`, or the last read."""
    deadline = time.monotonic() + timeout
    acts = world_map_acts(case)
    while time.monotonic() < deadline and acts != want:
        time.sleep(0.3)
        acts = world_map_acts(case)
    return acts


def wait_labels(case: Case, done, timeout: float = 10) -> dict[str, str]:
    """Every item's text once `done` holds of it, or the last read."""
    deadline = time.monotonic() + timeout
    labels = menu_labels(case)
    while time.monotonic() < deadline and not done(labels):
        time.sleep(0.3)
        labels = menu_labels(case)
    return labels


def shows_new_count(labels: dict[str, str], before: dict[str, str]) -> bool:
    """The shop's gear count is written (\"n/m\") and is not the old one."""
    count = labels.get("inventory_count", "")
    return re.fullmatch(r"\d+/\d+", count) is not None \
        and count != before.get("inventory_count")


def wait_tabs(case: Case, pattern: str, done, timeout: float = 10
              ) -> dict[str, bool]:
    """The game's tabs matching `pattern` once `done` holds of them."""
    deadline = time.monotonic() + timeout
    tabs = option_tabs(case, pattern)
    while time.monotonic() < deadline and not done(tabs):
        time.sleep(0.3)
        tabs = option_tabs(case, pattern)
    return tabs


def case_touch_stash(case: Case) -> None:
    """CMenuShop as the stash, drawn natively, opened by the console.

    The party starts with no gear, so the case buys one piece in the shop by
    touch (two temp_addmoney runs pay for it), then stores it and takes it
    back. Each tap is judged by the game's own state: the lit tab, the list's
    entries and the stash and gear counts.
    """
    if not into_gameplay(case, money_runs=2):
        return
    case.http("/console?command=openmenu%20shop")
    wait_settled_menu(case, "shop", 20)
    tap_tab(case, r"shop_option0\d", "buy")
    wait_tabs(case, r"shop_option0\d", lambda t: t.get("buy") is True)
    box = menu_list(case)
    gear = next((i for i, e in enumerate(box.get("entries", []))
                 if "Pack" not in e and menu_text(e) != "Grab Bag"), -1)
    case.check("the buy tab offers gear", gear >= 0, str(box.get("entries")))
    if gear < 0:
        return
    name = menu_text(box["entries"][gear])
    select_list_entry(case, gear)
    before = menu_labels(case)
    tap_list_entry(case, gear)
    # The shop blanks its count for one frame before writing the new one.
    labels = wait_labels(case, lambda now: shows_new_count(now, before))
    case.check("a tap on the selected %s bought it" % name,
               labels.get("inventory_count") == "1/20"
               and labels.get("money_value") != before.get("money_value"),
               "gear %s -> %s, money %s -> %s" % (
                   before.get("inventory_count"), labels.get("inventory_count"),
                   before.get("money_value"), labels.get("money_value")))
    tap_footer_button(case, "$MENU_OK")
    wait_game_menu(case, lambda m: m.get("menu") != "shop", 10)

    case.http("/console?command=openmenu%20stash")
    wait_settled_menu(case, "stash", 20)
    if option_tabs(case, r"stash_option0\d").get("stash"):
        # Opened on the stash tab and not stepped to inventory: open it.
        tap_tab(case, r"stash_option0\d", "inventory")
        wait_tabs(case, r"stash_option0\d",
                  lambda t: t.get("inventory") is True)
        wait_settled_menu(case, "stash", 20)
    (case.dir / "stash.json").write_text(
        json.dumps(menu_items(case), indent=1) + "\n")
    if not check_native_list(case, "CMenuShop"):
        return
    tabs = list(option_tabs(case, r"stash_option0\d").items())
    case.check("its tabs are stash and inventory with inventory lit",
               tabs == [("stash", False), ("inventory", True)], str(tabs))
    entries = [menu_text(e) for e in menu_list(case).get("entries", [])]
    case.check("its list is the party's gear", entries == [name], str(entries))
    labels = menu_labels(case)
    case.check("with the stash count and the gear's description",
               menu_text(labels.get("label_inventory_count", "")) == "stash"
               and labels.get("inventory_count") == "0/60"
               and bool(menu_text(labels.get("item_desc", ""))),
               "%r %r, desc %r" % (labels.get("label_inventory_count"),
                                   labels.get("inventory_count"),
                                   labels.get("item_desc")))
    if entries != [name]:
        return
    case.shot("stash")

    select_list_entry(case, 0)
    before = menu_labels(case)
    tap_list_entry(case, 0)
    wait_list(case, lambda b: not b.get("entries"))
    labels = wait_labels(case, lambda now: shows_new_count(now, before))
    case.check("a tap on the selected %s stored it" % name,
               labels.get("inventory_count") == "1/60"
               and not menu_list(case).get("entries"),
               "stash %s, inventory %s" % (labels.get("inventory_count"),
                                           menu_list(case).get("entries")))
    case.check("and the game keeps its tabs over the empty list",
               len(option_tabs(case, r"stash_option0\d")) == 2,
               str(option_tabs(case, r"stash_option0\d")))
    case.shot("stored")

    tap_tab(case, r"stash_option0\d", "stash")
    box = wait_list(case, lambda b: [menu_text(e) for e in b.get(
        "entries", [])] == [name])
    tabs = option_tabs(case, r"stash_option0\d")
    case.check("a tap on the stash tab opened it in the game",
               tabs == {"stash": True, "inventory": False}
               and [menu_text(e) for e in box.get("entries", [])] == [name],
               "%s, entries %s" % (tabs, box.get("entries")))
    case.shot("stash-tab")

    select_list_entry(case, 0)
    before = menu_labels(case)
    tap_list_entry(case, 0)
    wait_list(case, lambda b: not b.get("entries"))
    labels = wait_labels(case, lambda now: shows_new_count(now, before))
    case.check("a tap on it took it back to the inventory",
               labels.get("inventory_count") == "1/20",
               "gear %s" % labels.get("inventory_count"))
    case.check("the stash offers Accept", tap_footer_button(case, "$MENU_OK"))
    left = wait_game_menu(case, lambda m: m.get("menu") != "stash", 10)
    case.check("Accept left the stash",
               left.get("menu") != "stash", str(left.get("menu")))


def case_touch_review(case: Case) -> None:
    """CMenuReviewPaths drawn natively, opened by the console.

    It opens on the stats tab. A tap on the cinematics tab opens it in the
    game, and two taps on Credits select it and press A, which plays the
    credits; Esc returns to the review and Back leaves it.
    """
    if not into_gameplay(case):
        return
    case.http("/console?command=openmenu%20review")
    wait_settled_menu(case, "review", 20)
    (case.dir / "review.json").write_text(
        json.dumps(menu_items(case), indent=1) + "\n")
    if not check_native_list(case, "CMenuReviewPaths"):
        return
    tabs = list(option_tabs(case, r"option0\d_text").items())
    case.check("its tabs are the game's five with stats lit",
               tabs == [("screens", False), ("cinematics", False),
                        ("comics", False), ("concepts", False),
                        ("stats", True)], str(tabs))
    box = menu_list(case)
    entries = [menu_text(e) for e in box.get("entries", [])]
    values = box.get("values", [])
    case.check("its list is the stats entries with their counts",
               len(entries) > 1 and entries[1] == "Comic Books"
               and len(values) > 1 and menu_text(values[1]) == "0 of 3",
               str(list(zip(entries, values, strict=False))[:3]))
    case.shot("stats")

    tap_tab(case, r"option0\d_text", "cinematics")
    box = wait_list(case, lambda b: list_entry(b, "credits") >= 0)
    credits = list_entry(box, "credits")
    case.check("a tap on the cinematics tab opened it in the game",
               option_tabs(case, r"option0\d_text").get("cinematics") is True
               and credits >= 0,
               "%s, entries %s" % (option_tabs(case, r"option0\d_text"),
                                   box.get("entries")))
    if credits < 0:
        return
    case.shot("cinematics")

    box = select_list_entry(case, credits)
    case.check("a tap on Credits selected it", box.get("selected") == credits,
               "selected %s" % box.get("selected"))
    tap_list_entry(case, credits)
    shown = wait_game_menu(case, lambda m: m.get("menu") == "credits", 10)
    case.check("a tap on the selected Credits played them",
               shown.get("class") == "CMenuCredits",
               "%s %s" % (shown.get("menu"), shown.get("class")))
    case.shot("credits")

    case.http("/key?name=Escape&hold=0.2")
    back = wait_game_menu(case, lambda m: m.get("menu") == "review", 10)
    case.check("Esc returned to the review",
               back.get("menu") == "review",
               str(back.get("menu")))
    case.check("the review offers Back", tap_footer_button(case, "$MENU_BACK"))
    left = wait_game_menu(case, lambda m: m.get("menu") != "review", 10)
    case.check("Back left the review",
               left.get("menu") != "review", str(left.get("menu")))


def case_touch_region(case: Case) -> None:
    """CMenuRegion drawn natively, opened by the console.

    With no online service the game's region list is empty. Its Back,
    Refresh and Select footers are finger buttons; a tap on Select opens the
    campaign lobby as the game's own A does, and Back on a reopened region
    leaves it.
    """
    if not into_gameplay(case):
        return
    case.http("/console?command=openmenu%20region")
    wait_settled_menu(case, "region", 20)
    (case.dir / "region.json").write_text(
        json.dumps(menu_items(case), indent=1) + "\n")
    menu = read_menu(case)
    case.check("the game's CMenuRegion is up",
               menu.get("class") == "CMenuRegion", str(menu.get("class")))
    case.check("its Back, Refresh and Select are finger buttons",
               all(footer_button(menu, token) for token in
                   ("$MENU_BACK", "$MENU_SUBTRACT", "$MENU_ACCEPT")),
               str(menu.get("touch_buttons")))
    case.shot("region")

    tap_footer_button(case, "$MENU_ACCEPT")
    lobby = wait_game_menu(case, lambda m: m.get("menu") == "campaign_lobby",
                           10)
    case.check("a tap on Select opened the campaign lobby",
               lobby.get("class") == "CMenuCampaignLobby",
               "%s %s" % (lobby.get("menu"), lobby.get("class")))
    case.shot("lobby")

    case.http("/console?command=openmenu%20region")
    wait_settled_menu(case, "region", 20)
    case.check("the region offers Back", tap_footer_button(case, "$MENU_BACK"))
    left = wait_game_menu(case, lambda m: m.get("menu") != "region", 10)
    case.check("Back left the region",
               left.get("menu") != "region", str(left.get("menu")))


def case_touch_danger_room(case: Case) -> None:
    """CMenuDangerRoom drawn natively, opened by the console.

    A tap on the selected grade opens its courses, a tap on the Status tab
    opens it in the game, Back returns to the grades, and a tap on the
    selected course opens the team menu to choose who runs it.
    """
    if not into_gameplay(case):
        return
    case.http("/console?command=openmenu%20danger_room")
    wait_settled_menu(case, "danger_room", 20)
    (case.dir / "grades.json").write_text(
        json.dumps(menu_items(case), indent=1) + "\n")
    if not check_native_list(case, "CMenuDangerRoom"):
        return
    grades = menu_list(case).get("entries", [])
    case.check("its list is the game's grades with no tabs",
               grades[:1] == ["Freshman"]
               and not option_tabs(case, r"option_text\d"),
               "%s, tabs %s" % (grades, option_tabs(case, r"option_text\d")))
    desc = menu_labels(case).get("desc", "")
    menu = read_menu(case)
    case.check("with the game's description and Back and Select",
               menu_text(desc.split("\n")[0]) == "TRAINING MODE"
               and bool(menu_footer(menu, "$MENU_BACK"))
               and bool(menu_footer(menu, "$MENU_ACCEPT")),
               "desc %r" % desc)
    if not grades:
        return
    case.shot("grades")

    def open_freshman() -> list[str]:
        select_list_entry(case, 0)
        tap_list_entry(case, 0)
        return wait_list(case, lambda b: b.get("entries") != grades).get(
            "entries", [])

    courses = open_freshman()
    case.check("a tap on the selected Freshman opened its courses",
               courses[:1] == ["Setting 101 - Hidden Goods"]
               and option_tabs(case, r"option_text\d")
               == {"Overview": True, "Status": False},
               "%s, %s" % (courses, option_tabs(case, r"option_text\d")))
    case.check("and its cards", bool(list_cards(case)), str(list_cards(case)))
    case.shot("courses")

    tap_tab(case, r"option_text\d", "Status")
    tabs = wait_tabs(case, r"option_text\d", lambda t: t.get("Status") is True)
    desc = wait_labels(case, lambda now: menu_text(
        now.get("desc", "").split("\n")[0]) == "Status: Incomplete").get(
            "desc", "")
    case.check("a tap on Status opened it in the game",
               tabs == {"Overview": False, "Status": True}
               and menu_text(desc.split("\n")[0]) == "Status: Incomplete",
               "%s, desc %r" % (tabs, desc))
    case.shot("status")

    tap_footer_button(case, "$MENU_BACK")
    box = wait_list(case, lambda b: b.get("entries") == grades)
    case.check("Back returned to the grades", box.get("entries") == grades,
               str(box.get("entries")))

    courses = open_freshman()
    course = list_entry(menu_list(case), "setting 101 - hidden goods")
    case.check("Freshman's courses again", course >= 0, str(courses))
    if course < 0:
        return
    select_list_entry(case, course)
    tap_list_entry(case, course)
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
