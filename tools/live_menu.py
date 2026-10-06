"""Helpers for live cases on the retail menus and the touch menu: reading
GET /menu, reaching a menu, and tapping touch menu buttons."""

from __future__ import annotations

import json
import re
import time

from live_harness import Case


def wait_movie_end(case: Case, seen: int, timeout: float) -> float | None:
    """When the (seen+1)-th movie printed its end summary, or None.

    The frame counters in that summary cannot say whether a movie was skipped:
    the decoder runs ahead and the summary is printed at unload, so a skipped
    movie still reports what it had already decoded -- measured at 312 frames
    either way. WHEN it ended is the measure that separates them.
    """
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        if len(re.findall(r"native FMV: \d+ video decoded",
                          case.log_text())) > seen:
            return time.monotonic()
        time.sleep(0.5)
    return None


def keyboard_past_the_intro(case: Case, timeout: float) -> bool:
    """Reach the main menu by key, which is what a phone player cannot do.

    Getting there is not what this case tests; what a finger does once there
    is. Returns whether the movies stopped coming and the game's own menu is
    the main menu.
    """
    deadline = time.monotonic() + timeout
    loads, quiet = case.log_text().count("movie: loaded native"), 0
    while time.monotonic() < deadline and quiet < 4:
        case.http("/key?name=Escape&hold=0.3")
        time.sleep(1.0)
        case.http("/key?name=Return&hold=0.3")
        time.sleep(2.0)
        now = case.log_text().count("movie: loaded native")
        quiet = quiet + 1 if now == loads else 0
        loads = now
    case.http("/key?name=Escape&hold=0.3")
    time.sleep(3.0)
    return quiet >= 4 and wait_menu(case, "main", 30).get("menu") == "main"


MAIN_MENU_LABELS = ["new game", "load game", "danger room", "review",
                    "options", "play online", "quit"]


OPTIONS_MENU_LABELS = ["effects volume", "music volume", "combat music",
                       "view angle", "view cycle", "view follow", "view shake",
                       "subtitles", "vibration", "accept"]


class MenuTimeline:
    """GET /menu sampled until a condition holds, each sample written down."""

    def __init__(self, case: Case, name: str) -> None:
        self.case = case
        self.path = case.dir / name
        self.start = time.monotonic()

    def until(self, done, timeout: float) -> dict:
        deadline = time.monotonic() + timeout
        menu = read_menu(self.case)
        with self.path.open("a") as out:
            while True:
                shown = menu.get("touch_menu", {})
                out.write("%6.2f frames=%d menu=%s popup=%s focus=%s "
                          "touch=%s/%s\n" % (
                              time.monotonic() - self.start,
                              self.case.status_frames(), menu.get("menu"),
                              menu.get("popup"), menu.get("focused_row"),
                              shown.get("visible"), shown.get("menu")))
                if done(menu) or time.monotonic() >= deadline:
                    return menu
                time.sleep(0.1)
                menu = read_menu(self.case)


def read_menu(case: Case) -> dict:
    code, body = case.http("/menu")
    if code != 200:
        return {}
    try:
        return json.loads(body)
    except ValueError:
        return {}


def wait_menu(case: Case, name: str, timeout: float) -> dict:
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        menu = read_menu(case)
        if menu.get("menu") == name and menu.get("rows") \
                and menu.get("focused_row", -1) >= 0:
            return menu
        time.sleep(0.5)
    return read_menu(case)


def row_labels(menu: dict) -> list[str]:
    return [row["label"].lower() for row in menu.get("rows", [])]


def walk_rows_by_key(case: Case, menu: dict) -> list[int]:
    """Press Down once per row and record where the game's own focus went."""
    seen = []
    for _ in range(len(menu.get("rows", []))):
        case.http("/key?name=Down&hold=0.15")
        time.sleep(0.8)
        seen.append(read_menu(case).get("focused_row", -1))
    return seen


def open_options(case: Case) -> dict:
    rows = row_labels(read_menu(case))
    focus = read_menu(case).get("focused_row", -1)
    for _ in range((rows.index("options") - focus) % len(rows)):
        case.http("/key?name=Down&hold=0.15")
        time.sleep(0.8)
    case.http("/key?name=Return&hold=0.15")
    return wait_menu(case, "options", 20)


def touch_menu(case: Case) -> dict:
    """The touch menu as GET /menu reports it: rows, buttons, viewport."""
    return read_menu(case).get("touch_menu", {})


def wait_touch_menu(case: Case, name: str, timeout: float) -> dict:
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        shown = touch_menu(case)
        if shown.get("visible") and shown.get("menu") == name:
            return shown
        time.sleep(0.5)
    return touch_menu(case)


def touch_button(shown: dict, part: str, label: str) -> dict | None:
    for button in shown.get("buttons", []):
        if button["part"] == part and button["label"].lower() == label:
            return button
    return None


def tap_touch_button(case: Case, shown: dict, button: dict) -> bool:
    """Tap a touch menu button's centre, in fractions of ITS viewport."""
    return case.http("/touch?x=%g&y=%g" % (
        (button["left"] + button["width"] * 0.5) / shown["viewport_width"],
        (button["top"] + button["height"] * 0.5) / shown["viewport_height"])
    )[0] == 200


def touch_row(shown: dict, label: str) -> dict:
    for row in shown.get("rows", []):
        if row["label"].lower() == label:
            return row
    return {}


def wait_row_change(case: Case, label: str, key: str, before,
                    timeout: float) -> dict:
    deadline = time.monotonic() + timeout
    row = {}
    while time.monotonic() < deadline:
        row = touch_row(touch_menu(case), label)
        if row and row.get(key) != before:
            return row
        time.sleep(0.5)
    return row


def keyboard_into_the_pda(case: Case, timeout: float) -> dict:
    """From a Continue boot, Enter through any conversation and Escape until
    the game's own menu is the PDA."""
    deadline = time.monotonic() + timeout
    menu = read_menu(case)
    while time.monotonic() < deadline and menu.get("menu") != "pda":
        case.http("/key?name=Return&hold=0.2")
        time.sleep(1.0)
        case.http("/key?name=Escape&hold=0.2")
        time.sleep(1.5)
        menu = read_menu(case)
    return menu


def wait_game_menu(case: Case, done, timeout: float) -> dict:
    deadline = time.monotonic() + timeout
    menu = read_menu(case)
    while time.monotonic() < deadline and not done(menu):
        time.sleep(0.3)
        menu = read_menu(case)
    return menu


def shop_list(case: Case) -> dict:
    """The shop's list box as GET /menu?items=all reads it."""
    code, body = case.http("/menu?items=all")
    if code != 200:
        return {}
    try:
        menu = json.loads(body)
    except ValueError:
        return {}
    for item in menu.get("items", []):
        if item.get("name") == "list" and "list_box" in item:
            return item["list_box"]
    return {}
