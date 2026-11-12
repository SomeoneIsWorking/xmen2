"""Live case for the port's Controls and Key Bindings pages (plan #203).

`options-pages` opens the game's Options from the main menu in a 1600x900
window and walks both pages by keyboard and mouse:

- Options lists Controls and Key Bindings; a click on Controls opens the
  Controls page, which lists the four keyboards and the controller slots,
  and Right on Keyboard 2 assigns it and saves it;
- Escape returns to Options, not the main menu;
- Return on Key Bindings opens that page; Return on Move Forward waits for a
  key, T becomes its binding and is saved, and neither T nor the Return that
  started it reaches the menu; Escape while waiting cancels and leaves the
  page up; Left gives Move Forward the game's key back;
- Escape twice returns through Options to the main menu.

Judged by GET /menu (rows, values, the open menu), the log and
x2native.conf, with a shot of each page.
"""

from __future__ import annotations

import time

from live_cases_options_rows import (boot_to_options, focused, key_to, labels,
                                     row, setting)
from live_harness import Case
from live_menu import read_menu, wait_menu

KEYBOARDS = ["keyboard 1", "keyboard 2", "keyboard 3", "keyboard 4"]


def value(case: Case, label: str) -> str:
    return str(row(read_menu(case), label).get("value") or "").lower()


def logged(case: Case, text: str, before: int, timeout: float = 10.0) -> bool:
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        if case.log_text().count(text) > before:
            return True
        time.sleep(0.3)
    return False


def key(case: Case, name: str) -> None:
    case.http("/key?name=%s&hold=0.15" % name)
    time.sleep(0.8)


def host_key(case: Case, name: str) -> None:
    """A key as the window gets it, which is what a capture listens to."""
    case.http("/ui/key?name=%s" % name)
    time.sleep(0.8)


def case_options_pages(case: Case) -> None:
    menu = boot_to_options(case, "pages", ["video.mode=windowed",
                                           "video.width=1600",
                                           "video.height=900"])
    names = labels(menu)
    case.check("Options lists Controls and Key Bindings",
               "controls" in names and "key bindings" in names, str(names))
    tap = row(menu, "controls").get("tap")
    case.check("Controls has a place on screen", bool(tap))
    if not tap:
        return
    case.http("/ui/click?x=%d&y=%d" % (tap["x"] * 1600, tap["y"] * 900))
    menu = wait_menu(case, "port_controls", 20)
    time.sleep(1.5)
    menu = read_menu(case)
    case.shot("controls")
    names = labels(menu)
    case.check("a click on Controls opens the Controls page",
               menu.get("menu") == "port_controls", str(menu.get("menu")))
    case.check("the page lists the four keyboards and the controllers",
               all(k in names for k in KEYBOARDS) and "accept" in names
               and sum(n.startswith("controller") or n not in KEYBOARDS
                       for n in names) >= 6, str(names))
    case.check("no retail setting is on the page",
               "vibration" not in names and "resolution" not in names,
               str(names))
    before = setting(case, "input.keyboard1.player")
    count = case.log_text().count("PORT OPTIONS: Keyboard 2 is now")
    case.check("the keyboard reaches Keyboard 2", key_to(case, "keyboard 2"),
               focused(read_menu(case)))
    key(case, "Right")
    case.check("Right on Keyboard 2 assigns it and saves it",
               logged(case, "PORT OPTIONS: Keyboard 2 is now", count)
               and setting(case, "input.keyboard1.player") != before,
               "%s -> %s, shows %s" % (before,
                                       setting(case, "input.keyboard1.player"),
                                       value(case, "keyboard 2")))
    key(case, "Escape")
    menu = wait_menu(case, "options", 10)
    case.check("Escape on Controls returns to Options",
               menu.get("menu") == "options", str(menu.get("menu")))

    case.check("the keyboard reaches Key Bindings",
               key_to(case, "key bindings"), focused(read_menu(case)))
    key(case, "Return")
    menu = wait_menu(case, "port_bindings", 20)
    time.sleep(1.5)
    case.shot("bindings")
    names = labels(read_menu(case))
    case.check("Return on Key Bindings opens its page",
               read_menu(case).get("menu") == "port_bindings"
               and "move forward" in names and "restore all defaults" in names,
               str(names))
    game_key = value(case, "move forward")
    case.check("the keyboard reaches Move Forward", key_to(case, "move forward"),
               focused(read_menu(case)))
    key(case, "Return")
    case.check("Return on Move Forward waits for a key",
               value(case, "move forward") == "press a key",
               value(case, "move forward"))
    case.shot("waiting")
    count = case.log_text().count("BINDING CAPTURE: keyboard 1 row 0 is now")
    host_key(case, "T")
    case.check("T becomes Move Forward and is saved",
               logged(case, "BINDING CAPTURE: keyboard 1 row 0 is now DIK 0x14",
                      count)
               and value(case, "move forward") == "t"
               and setting(case, "input.profile0.row0") != "",
               "shows %s, saved %r" % (value(case, "move forward"),
                                       setting(case, "input.profile0.row0")))
    case.check("neither T nor Return reached the menu",
               read_menu(case).get("menu") == "port_bindings"
               and focused(read_menu(case)) == "move forward",
               "%s on %s" % (read_menu(case).get("menu"),
                             focused(read_menu(case))))
    key(case, "Return")
    host_key(case, "Escape")
    case.check("Escape while waiting cancels and keeps the page",
               read_menu(case).get("menu") == "port_bindings"
               and value(case, "move forward") == "t",
               "%s, shows %s" % (read_menu(case).get("menu"),
                                 value(case, "move forward")))
    count = case.log_text().count("PORT OPTIONS: Move Forward is now")
    key(case, "Left")
    case.check("Left gives Move Forward the game's key back",
               logged(case, "PORT OPTIONS: Move Forward is now", count)
               and value(case, "move forward") == game_key,
               "%s, game's %s" % (value(case, "move forward"), game_key))
    key(case, "Escape")
    menu = wait_menu(case, "options", 10)
    case.check("Escape on Key Bindings returns to Options",
               menu.get("menu") == "options", str(menu.get("menu")))
    key(case, "Escape")
    menu = wait_menu(case, "main", 10)
    case.check("Escape on Options returns to the main menu",
               menu.get("menu") == "main", str(menu.get("menu")))
