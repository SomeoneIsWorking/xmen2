"""Live case for the online hero list on touch (docs/touch-play.md).

The run boots into act2/jungle/jungle1, taps through the level's opening
line, and opens CMenuHeroList (Select Hero) with the console's
`openmenu hero_list`. It judges by GET /menu that the list's heroes are cards at least a finger apart and Back and Select
finger buttons, that a drag brings the last hero into view, a tap selects
it and a second tap accepts it, closing the list, and that Back on a
reopened list closes it.
"""

from __future__ import annotations

import itertools
import time

from live_cases_touch_map import inside_window, into_level
from live_harness import Case
from live_menu import (finger_tall, footer_button, list_cards, list_entry,
                       menu_list, read_menu, reveal_list_entry,
                       tap_button_edge, tap_menu_item, wait_list)

SHAPES = {
    "touch-hero-list": (1280, 720),
    "touch-hero-list-4x3": (960, 720),
    "touch-hero-list-tablet": (1216, 800),
    "touch-hero-list-20x9": (1600, 720),
}
LIST = "text_list"
LAST = "sabretooth"


def open_list(case: Case) -> dict:
    case.http("/console?command=openmenu%20hero_list")
    deadline = time.monotonic() + 20
    menu = {}
    while time.monotonic() < deadline:
        menu = read_menu(case)
        if (menu.get("class") == "CMenuHeroList"
                and len(menu.get("touch_buttons", [])) >= 2
                and list_cards(case, LIST)):
            break
        time.sleep(0.5)
    time.sleep(0.5)
    return read_menu(case)


def closed(case: Case) -> bool:
    deadline = time.monotonic() + 10
    while time.monotonic() < deadline:
        if read_menu(case).get("class") != "CMenuHeroList":
            return True
        time.sleep(0.3)
    return False


def finger_window(button: dict) -> float:
    """A finger's height as a share of the window, from a finger button's
    centre and its edge point (nine tenths up)."""
    return (button["tap"]["y"] - button["edge"]["y"]) / 0.4 / button["fingers"]


def case_touch_hero_list(case: Case) -> None:
    """CMenuHeroList native on touch: scroll, select, accept and Back."""
    case.check("the run reached the level's gameplay",
               into_level(case, *SHAPES[case.name]))
    menu = open_list(case)
    case.check("the game's hero list is up",
               menu.get("class") == "CMenuHeroList", str(menu.get("class")))
    cards = list_cards(case, LIST)
    case.check("the list is drawn as touch cards", len(cards) >= 2,
               "%d card(s)" % len(cards))
    if menu.get("class") != "CMenuHeroList" or len(cards) < 2:
        case.shot("no-list")
        return
    case.shot("open")
    back = footer_button(menu, "$MENU_BACK")
    select = footer_button(menu, "$MENU_ACCEPT")
    case.check("Back and Select are finger buttons in the window",
               all(finger_tall(b) and inside_window(b) for b in (back, select)),
               "%s, %s" % (back.get("fingers"), select.get("fingers")))
    if not back:
        return
    finger = finger_window(back)
    pitch = min(b["tap"]["y"] - a["tap"]["y"] for a, b in itertools.pairwise(cards))
    case.check("the hero cards are at least a finger apart",
               pitch >= finger * 0.999,
               "pitch %.4f, finger %.4f of the window" % (pitch, finger))

    last = list_entry(menu_list(case, LIST), LAST)
    card = reveal_list_entry(case, last, LIST)
    case.check("a drag brings the last hero into view", card is not None,
               "entry %d" % last)
    if card is None:
        return
    case.shot("scrolled")
    tap_menu_item(case, card)
    box = wait_list(case, lambda b: b.get("selected") == last, LIST)
    case.check("a tap on a hero's card selects it",
               box.get("selected") == last, str(box.get("selected")))
    card = next((c for c in list_cards(case, LIST) if c["entry"] == last),
                card)
    tap_menu_item(case, card)
    case.check("a tap on the selected hero accepts it and closes the list",
               closed(case))

    menu = open_list(case)
    back = footer_button(menu, "$MENU_BACK")
    tap_button_edge(case, back)
    case.check("a tap on Back's edge closes the list", closed(case))
    case.shot("after")
