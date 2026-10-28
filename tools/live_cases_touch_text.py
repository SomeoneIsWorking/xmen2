"""Live case for the game's text entry on touch (docs/touch-play.md).

The run boots into act2/jungle/jungle1, taps through the level's opening
line, and opens CMenuTextEntry with the console's `openmenu text_entry`.
Text is typed as the platform's text input delivers it (GET /ui/text, one
event for a whole word, as a phone's keyboard commits it) and Backspace as
a host key. It judges by the entry's own field (`data_playername` in GET
/menu?items=all) that every character of the word lands and that Backspace
takes one off; that Done and Back are finger buttons, with no menu pad drawn
over the screen, and each closes the entry; and by GET /menu's `text_input`
that the platform's text input (a phone's screen keyboard) is on exactly
while the entry is up.
"""

from __future__ import annotations

import time
import urllib.parse

from live_cases_touch_map import disjoint, inside_window, into_level, tap
from live_game import live_controls
from live_harness import Case
from live_menu import finger_tall, read_menu, read_menu_items

SHAPES = {
    "touch-text-entry": (1280, 720),
    "touch-text-entry-4x3": (960, 720),
    "touch-text-entry-tablet": (1216, 800),
    "touch-text-entry-20x9": (1600, 720),
}
FIELD = "data_playername"


def field(case: Case) -> str | None:
    """What the entry's field shows, or None with no field read."""
    for item in read_menu_items(case).get("items", []):
        if item.get("name") == FIELD:
            return item.get("label")
    return None


def text_input_on(case: Case) -> bool:
    return read_menu(case).get("text_input") is True


def open_entry(case: Case) -> dict:
    case.http("/console?command=" + urllib.parse.quote("openmenu text_entry"))
    deadline = time.monotonic() + 20
    menu = {}
    while time.monotonic() < deadline:
        menu = read_menu(case)
        if (menu.get("class") == "CMenuTextEntry"
                and len(menu.get("touch_buttons", [])) >= 2):
            break
        time.sleep(0.5)
    time.sleep(0.5)
    return menu


def closed(case: Case) -> bool:
    deadline = time.monotonic() + 10
    while time.monotonic() < deadline:
        if read_menu(case).get("active") is False:
            return True
        time.sleep(0.3)
    return False


def settle(case: Case, want: str) -> str | None:
    """The field once it reads `want`, or what it reads after a few
    seconds."""
    deadline = time.monotonic() + 5
    shown = field(case)
    while shown != want and time.monotonic() < deadline:
        time.sleep(0.2)
        shown = field(case)
    return shown


def case_touch_text_entry(case: Case) -> None:
    """CMenuTextEntry native on touch: typing, Backspace, Done and Back."""
    case.check("the run reached the level's gameplay",
               into_level(case, *SHAPES[case.name]))
    menu = open_entry(case)
    case.check("the game's text entry is up",
               menu.get("class") == "CMenuTextEntry", str(menu.get("class")))
    start = field(case)
    case.check("GET /menu reads the entry's field", start is not None,
               str(start))
    if menu.get("class") != "CMenuTextEntry" or start is None:
        case.shot("no-entry")
        return
    case.shot("open")
    controls = live_controls(case)
    case.check("no menu pad is drawn over it",
               not any(name.startswith("menu-") for name in controls),
               "drawn: %s" % sorted(controls))
    back, done = sorted(menu["touch_buttons"], key=lambda b: b["tap"]["x"])[:2]
    case.check("Back and Done are finger buttons in the window",
               all(finger_tall(b) and inside_window(b) for b in (back, done)),
               "%s, %s" % (back.get("fingers"), done.get("fingers")))
    case.check("Back and Done are disjoint",
               disjoint(back["area"], done["area"]),
               "%s, %s" % (back["area"], done["area"]))

    case.http("/ui/text?text=Logan")
    shown = settle(case, start + "Logan")
    case.check("a word typed at once lands whole", shown == start + "Logan",
               repr(shown))
    case.http("/ui/key?name=Backspace")
    name = start + "Loga"
    shown = settle(case, name)
    case.check("Backspace takes one character off", shown == name,
               repr(shown))
    case.shot("typed")

    tap(case, done["edge"])
    case.check("a tap on Done's edge accepts the name and closes the entry",
               closed(case))
    case.check("the platform's text input goes off with the entry",
               not text_input_on(case))
    open_entry(case)
    case.check("the platform's text input comes on with the entry",
               text_input_on(case))
    tap(case, back["edge"])
    case.check("a tap on Back's edge closes the entry", closed(case))
    case.shot("after")
