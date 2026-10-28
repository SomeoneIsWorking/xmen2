"""Live cases for a finger on the retail row menus, the game's own screens:
the main menu, Options and the PDA."""

from __future__ import annotations

import itertools
import json
import time

from live_game import live_controls
from live_harness import Case
from live_menu import (
    MAIN_MENU_LABELS,
    drag_between,
    finger_tall,
    footer_button,
    item_button,
    leave_to,
    menu_row,
    read_menu,
    read_menu_items,
    row_buttons,
    setting_cards,
    tap_button_edge,
    tap_footer_button,
    tap_point,
    tap_row_button,
    wait_game_menu,
    wait_menu,
    wait_menu_row_change,
)


# The game's own first open of Options: the menu animates, so a pixel delta
# cannot tell whether a row was activated.
OPTIONS_PACKAGE = "menus/options.pkgb"


# What a tap on each main menu row opens that a finger can leave again: a
# popup, or a menu by name.
MAIN_ROW_OPENS = {"new game": "popup", "load game": "popup",
                  "danger room": "popup", "review": "review",
                  "play online": "online"}
OPTIONS_SETTINGS = ["effects volume", "music volume", "combat music",
                    "view angle", "view cycle", "view follow", "view shake",
                    "subtitles", "vibration"]


def case_touch_menu(case: Case) -> None:
    """A finger on the retail main and Options menus, the game's own screens.

    Every main row is a finger-tall area across its bar and each tap lands at
    its edge, outside the game's own box; Options' settings are finger cards
    in a column a drag scrolls, a tap on a toggle changes it and a tap or a
    slide along a volume bar steps it toward the finger. Each tap is judged
    by the game's own state as GET /menu reads it: the menu or popup it has
    open, a volume bar's level, a toggle's value text. The negative comes
    first, so a run where every tap "works" cannot pass.
    """
    case.prepare_profile(["boot.mode=menu", "input.touch_controls=2"])
    case.launch({"X2_FILES": "1"})
    case.wait_control(60)
    main = wait_menu(case, "main", 300)
    case.check("the run reached the main menu", main.get("menu") == "main")
    time.sleep(1.0)
    main = read_menu_items(case)
    (case.dir / "main.json").write_text(json.dumps(main, indent=1) + "\n")
    controls = live_controls(case)
    case.check("the menu pad is not drawn over the main menu",
               "menu-a" not in controls, "drawn: %s" % sorted(controls))
    rows = row_buttons(main)
    case.check("every main row is a finger tall across its bar",
               list(rows) == MAIN_MENU_LABELS
               and all(finger_tall(button) for button in rows.values()),
               str({label: button.get("fingers")
                    for label, button in rows.items()}))
    case.shot("main")

    opened_before = case.log_text().count(OPTIONS_PACKAGE)
    case.http("/touch?x=0.5&y=0.02")
    time.sleep(3.0)
    case.check("a tap on no item activates nothing",
               case.log_text().count(OPTIONS_PACKAGE) == opened_before
               and read_menu(case).get("menu") == "main")

    for label, opens in MAIN_ROW_OPENS.items():
        tap_row_button(case, label)
        shown = wait_game_menu(
            case, lambda m, opens=opens: m.get("popup") is True
            if opens == "popup" else m.get("menu") == opens, 20)
        case.check("a tap at %s's edge opened its %s" % (label, opens),
                   (shown.get("popup") is True) if opens == "popup"
                   else shown.get("menu") == opens,
                   "menu %r popup %r" % (shown.get("menu"), shown.get("popup")))
        time.sleep(1.0)
        case.shot(label.replace(" ", "-"))
        back = leave_to(case, "main")
        case.check("and a finger left it for the main menu",
                   back.get("menu") == "main" and back.get("popup") is False,
                   "menu %r" % back.get("menu"))

    case.check("OPTIONS was tapped at its edge",
               tap_row_button(case, "options"))
    options = wait_menu(case, "options", 20)
    case.check("the tap opened the game's Options menu",
               options.get("menu") == "options",
               "the game's menu is %r" % options.get("menu"))
    if options.get("menu") != "options":
        return
    time.sleep(1.0)
    options = read_menu_items(case)
    (case.dir / "options.json").write_text(json.dumps(options, indent=1)
                                           + "\n")
    cards = setting_cards(options)
    case.check("Options' first settings are finger-tall cards in a column",
               list(cards) == OPTIONS_SETTINGS[:len(cards)]
               and len(cards) >= 4
               and all(card["fingers"] >= 1.0 - 1e-3 for card in cards.values())
               and all(a["tap"]["y"] < b["tap"]["y"] for a, b in
                       itertools.pairwise(cards.values())),
               str({label: card.get("fingers") for label, card in cards.items()}))
    accept = item_button(options, menu_row(options, "accept"))
    case.check("and Accept a finger tall across its bar", finger_tall(accept),
               str(accept))
    case.shot("options")

    toggle = menu_row(options, "combat music")
    before = toggle.get("value")
    case.check("the combat music row shows its value",
               before in ("On", "Off"), repr(before))
    tap_point(case, cards.get("combat music", {}).get("edge"))
    after = wait_menu_row_change(case, "combat music", "value", before, 10)
    case.check("a tap at its card's edge changed the game's value text",
               after.get("value") in ("On", "Off")
               and after.get("value") != before,
               "%r -> %r" % (before, after.get("value")))

    effects = cards.get("effects volume", {})
    before = menu_row(read_menu(case), "effects volume").get("fill")
    tap_point(case, effects.get("bar_low"))
    lowered = wait_settled_fill(case, "effects volume", before)
    case.check("a tap near the low end of its bar stepped the effects "
               "volume down to the finger",
               before is not None and lowered is not None
               and lowered < before and lowered <= 0.25,
               "%s -> %s" % (before, lowered))
    music = cards.get("music volume", {})
    before = menu_row(read_menu(case), "music volume").get("fill")
    if music.get("bar_low") and music.get("bar_high"):
        drag_along(case, music["bar_high"], music["bar_low"])
    slid = wait_settled_fill(case, "music volume", before)
    case.check("a slide along the music bar brought it down with the finger",
               before is not None and slid is not None and slid < before
               and slid <= 0.25,
               "%s -> %s" % (before, slid))
    time.sleep(1.0)
    case.shot("options-changed")

    shown = list(cards.values())
    drag_between(case, shown[-1]["tap"], shown[0]["tap"])
    time.sleep(1.5)
    scrolled = setting_cards(read_menu_items(case))
    case.check("a drag up scrolled the column to the last settings",
               list(scrolled) == OPTIONS_SETTINGS[-len(scrolled):]
               and "vibration" in scrolled,
               str(list(scrolled)))
    case.shot("options-scrolled")
    before = menu_row(read_menu(case), "vibration").get("value")
    tap_point(case, scrolled.get("vibration", {}).get("edge"))
    after = wait_menu_row_change(case, "vibration", "value", before, 10)
    case.check("and a tap on Vibration, scrolled into view, changed it",
               after.get("value") in ("On", "Off")
               and after.get("value") != before,
               "%r -> %r" % (before, after.get("value")))

    case.check("Back is a finger button and was tapped",
               tap_footer_button(case, "$MENU_BACK"),
               str(read_menu(case).get("touch_buttons")))
    left = wait_game_menu(case, lambda m: m.get("menu") not in (None,
                                                                 "options"),
                          20)
    case.check("Back left Options", left.get("menu") == "main",
               "the game's menu is now %r" % left.get("menu"))
    time.sleep(2.0)
    case.shot("after-back")

    tap_row_button(case, "quit")
    deadline = time.monotonic() + 30
    while time.monotonic() < deadline and case.alive():
        time.sleep(0.5)
    case.check("and a tap at Quit's edge quit the game", not case.alive())


# A level with every PDA row enabled, Blink Portal included.
PDA_MAP = "act2/jungle/jungle1"
PDA_LABELS = ["objectives", "team management", "blink portal", "small map",
              "automap", "options", "players", "load game", "quit game"]


def pda_row_did(label: str, before: dict, menu: dict) -> bool:
    """Whether the game's state after a tap on PDA row `label` is that row's
    own: its page, the menu it opens, its popup, or the PDA closed."""
    on_pda = menu.get("menu") == "pda"
    opens = {"objectives": on_pda and menu.get("mode") == 1,
             "team management": menu.get("menu") == "team",
             "blink portal": menu.get("active") is False,
             "automap": menu.get("menu") == "automap",
             "options": menu.get("menu") == "options",
             "players": on_pda and menu.get("mode") == 2,
             "load game": on_pda and menu.get("popup") is True,
             "quit game": on_pda and menu.get("popup") is True}
    if label in opens:
        return opens[label]
    # Small Map switches the map overlay to large and says so in its row.
    name = menu_row(before, label).get("name")
    shown = next((row.get("label") for row in menu.get("rows", [])
                  if row.get("name") == name), None)
    return on_pda and shown == "Large Map"


def open_pda_rows(case: Case) -> dict:
    """The PDA on its rows page: a finger's way back when there is one, else
    the console's openmenu."""
    menu = read_menu_items(case)
    if menu.get("menu") == "pda" and (menu.get("popup") or menu.get("mode")):
        menu = leave_pda_page(case)
    if menu.get("menu") != "pda" or menu.get("mode"):
        if menu.get("menu") == "options":
            menu = leave_to(case, "pda")
        else:
            case.http("/console?command=openmenu%20pda")
    menu = wait_game_menu(case, lambda m: m.get("menu") == "pda"
                          and m.get("mode") == 0 and m.get("popup") is False
                          and m.get("focused_row", -1) >= 0, 20)
    time.sleep(1.0)
    return read_menu_items(case)


def leave_pda_page(case: Case) -> dict:
    """Off a PDA popup or icon page by its Back, as a finger leaves."""
    menu = read_menu_items(case)
    if menu.get("popup"):
        return leave_to(case, "pda")
    tap_button_edge(case, footer_button(menu, "$MENU_BACK"))
    return wait_game_menu(case, lambda m: m.get("mode") == 0, 10)


def case_touch_pda(case: Case) -> None:
    """A finger on the PDA's rows, the game's own screen.

    Every row is a finger-tall area across its bar and each tap lands at its
    edge, outside the game's own box; each tap is judged by the game's own
    state: the PDA's page, the menu or popup it opened, or the PDA closed.
    """
    case.prepare_profile(["boot.mode=menu", "input.touch_controls=2"])
    case.launch({"X2_BOOT_MAP": PDA_MAP})
    case.wait_control(60)
    level = wait_game_menu(case, lambda m: m.get("active") is False
                           and case.status_frames() > 0, 300)
    case.check("the run reached the level", level.get("active") is False,
               str(level.get("menu")))
    time.sleep(5.0)
    pda = open_pda_rows(case)
    (case.dir / "pda.json").write_text(json.dumps(pda, indent=1) + "\n")
    rows = row_buttons(pda)
    case.check("every PDA row is a finger tall across its bar",
               list(rows) == PDA_LABELS
               and all(finger_tall(button) for button in rows.values()),
               str({label: button.get("fingers")
                    for label, button in rows.items()}))
    case.shot("pda")

    case.http("/touch?x=0.9&y=0.5")
    time.sleep(2.0)
    idle = read_menu(case)
    case.check("a tap on no item activates nothing",
               idle.get("menu") == "pda" and idle.get("mode") == 0
               and idle.get("popup") is False)

    for label in PDA_LABELS:
        before = open_pda_rows(case)
        tapped = tap_row_button(case, label)
        after = wait_game_menu(
            case, lambda m, label=label, before=before:
            pda_row_did(label, before, m), 20)
        after = read_menu_items(case)
        case.check("a tap at %s's edge did its own thing" % label,
                   tapped and pda_row_did(label, before, after),
                   "menu %r mode %r popup %r focused %r"
                   % (after.get("menu"), after.get("mode"),
                      after.get("popup"), after.get("focused_row")))
        time.sleep(1.0)
        case.shot("pda-" + label.replace(" ", "-"))
    back = open_pda_rows(case)
    case.check("and the PDA came back to its rows",
               back.get("menu") == "pda" and back.get("mode") == 0)


def wait_settled_fill(case: Case, label: str, before,
                      timeout: float = 10) -> float | None:
    """A volume row's level once it has moved from `before` and held still
    for a second, or the last read."""
    deadline = time.monotonic() + timeout
    fill = menu_row(read_menu(case), label).get("fill")
    still_since = time.monotonic()
    while time.monotonic() < deadline and (
            fill == before or time.monotonic() - still_since < 1.0):
        time.sleep(0.2)
        now = menu_row(read_menu(case), label).get("fill")
        if now != fill:
            still_since = time.monotonic()
        fill = now
    return fill


def drag_along(case: Case, start: dict, end: dict) -> None:
    """One finger from tap point `start` to `end` across, at `start`'s
    height, through the contact phases, held at the end until the bar has
    followed."""
    y = start["y"]
    steps = 6
    case.http("/touch?x=%g&y=%g&phase=down" % (start["x"], y))
    for i in range(1, steps + 1):
        time.sleep(0.15)
        case.http("/touch?x=%g&y=%g&phase=motion"
                  % (start["x"] + (end["x"] - start["x"]) * i / steps, y))
    time.sleep(2.0)
    case.http("/touch?x=%g&y=%g&phase=up" % (end["x"], y))
