"""Helpers for live cases on the retail menus: reading GET /menu, reaching a
menu, tapping the game's items, finger buttons and list cards."""

from __future__ import annotations

import json
import re
import time

from live_game import live_controls, live_viewport, tap_control
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
                out.write("%6.2f frames=%d menu=%s popup=%s focus=%s\n" % (
                    time.monotonic() - self.start, self.case.status_frames(),
                    menu.get("menu"), menu.get("popup"),
                    menu.get("focused_row")))
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


def menu_row(menu: dict, label: str) -> dict:
    """The game's row whose label is `label`, lower case."""
    for row in menu.get("rows", []):
        if row["label"].lower() == label:
            return row
    return {}


def menu_footer(menu: dict, token: str) -> dict:
    """The game's desctext item carrying `token`, such as $MENU_BACK; a
    menu whose only item is a footer has it in its rows."""
    for text in menu.get("texts", []) + menu.get("rows", []):
        if text["name"].startswith("desctext") and token in text["label"]:
            return text
    return {}


def item_button(menu: dict, item: dict) -> dict:
    """The finger button touch put over `item`: the one whose press is
    delivered inside the box the game hit-tests."""
    rect = item.get("rect")
    if rect is None:
        return {}
    # Neighbouring footers' boxes overlap; the aim nearest the box's middle
    # is this item's.
    middle = (0.5 * (rect["left"] + rect["right"]),
              0.5 * (rect["top"] + rect["bottom"]))
    inside = [b for b in menu.get("touch_buttons", [])
              if rect["left"] <= b["aim_x"] <= rect["right"]
              and rect["top"] <= b["aim_z"] <= rect["bottom"]]
    return min(inside, default={}, key=lambda b: (b["aim_x"] - middle[0]) ** 2
               + (b["aim_z"] - middle[1]) ** 2)


def finger_tall(button: dict) -> bool:
    """Whether a finger button is at least a finger tall."""
    return bool(button) and button.get("fingers", 0.0) >= 1.0 - 1e-4


def row_buttons(menu: dict) -> dict[str, dict]:
    """Each of the menu's rows by its lower-case label, with the finger
    button touch put over it, or {} for a row with none."""
    return {row["label"].lower(): item_button(menu, row)
            for row in menu.get("rows", [])}


def tap_row_button(case: Case, label: str) -> bool:
    """Tap the finger button over the row `label` at its edge, outside the
    game's own box."""
    return tap_button_edge(case, row_buttons(read_menu(case)).get(label, {}))


def tap_point(case: Case, point: dict | None) -> bool:
    """Tap a window-fraction point such as a `tap` or an `edge`."""
    return point is not None and case.http(
        "/touch?x=%g&y=%g" % (point["x"], point["y"]))[0] == 200


def setting_cards(menu: dict) -> dict[str, dict]:
    """Options' settings touch drew as cards, by lower-case label, each its
    `touch_card` (tap, edge, fingers and a volume's bar_low and bar_high)."""
    return {item["label"].lower(): item["touch_card"]
            for item in menu.get("items", []) if "touch_card" in item}


def leave_to(case: Case, name: str, timeout: float = 20) -> dict:
    """Back to the game's menu `name` the way a finger leaves: a popup's
    Back, No, Cancel or only button, the menu's Back footer button, or on a
    menu not native on touch the menu pad's B."""
    deadline = time.monotonic() + timeout
    menu = read_menu_items(case)
    while time.monotonic() < deadline and not (
            menu.get("menu") == name and menu.get("popup") is False):
        controls = live_controls(case)
        viewport = live_viewport(case)
        if menu.get("popup"):
            footers = (menu.get("popup_page") or {}).get("footers", [])
            out = next((f for f in footers if f.get("text", "").strip().lower()
                        in ("back", "no", "cancel")),
                       footers[0] if len(footers) == 1 else None)
            tap_point(case, out and out.get("tap"))
        elif footer_button(menu, "$MENU_BACK"):
            tap_button_edge(case, footer_button(menu, "$MENU_BACK"))
        elif "menu-b" in controls and viewport:
            tap_control(case, controls["menu-b"], viewport)
        time.sleep(2.0)
        menu = read_menu_items(case)
    return menu


def footer_button(menu: dict, token: str) -> dict:
    """The finger button over the desctext item carrying `token`."""
    return item_button(menu, menu_footer(menu, token))


def tap_button_edge(case: Case, button: dict) -> bool:
    """Tap a finger button near its top corner, outside the game's own box."""
    edge = button.get("edge")
    return edge is not None and case.http(
        "/touch?x=%g&y=%g" % (edge["x"], edge["y"]))[0] == 200


def tap_footer_button(case: Case, token: str) -> bool:
    """Tap the finger button over the footer carrying `token`."""
    return tap_button_edge(case, footer_button(read_menu(case), token))


def tap_menu_item(case: Case, item: dict) -> bool:
    """Tap the centre of the box the game hit-tests for `item`."""
    tap = item.get("tap")
    return tap is not None and case.http(
        "/touch?x=%g&y=%g" % (tap["x"], tap["y"]))[0] == 200


def wait_menu_row_change(case: Case, label: str, key: str, before,
                         timeout: float) -> dict:
    deadline = time.monotonic() + timeout
    row = {}
    while time.monotonic() < deadline:
        row = menu_row(read_menu(case), label)
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


def conversation_held(case: Case) -> bool:
    """The game's conversation is enabled or shown, as GET /party reads it;
    while it is, the party takes no damage and level-ups wait."""
    code, body = case.http("/party")
    return code != 200 or json.loads(body).get("conversation") is not False


def keyboard_into_gameplay(case: Case, timeout: float) -> dict:
    """From a Continue boot, into the PDA, Escape out of it to gameplay and
    Return through the level's opening conversation, which can start after
    the PDA closes."""
    menu = keyboard_into_the_pda(case, timeout)
    if menu.get("menu") != "pda":
        return menu
    deadline = time.monotonic() + 20
    while time.monotonic() < deadline and menu.get("active") is not False:
        case.http("/key?name=Escape&hold=0.2")
        menu = wait_game_menu(case, lambda m: m.get("active") is False, 3)
    deadline = time.monotonic() + 60
    while time.monotonic() < deadline and conversation_held(case):
        case.http("/key?name=Return&hold=0.2")
        time.sleep(1.0)
    held = conversation_held(case)
    case.check("no conversation holds the party", not held)
    return read_menu(case) if not held else {"active": None}


def wait_game_menu(case: Case, done, timeout: float) -> dict:
    deadline = time.monotonic() + timeout
    menu = read_menu(case)
    while time.monotonic() < deadline and not done(menu):
        time.sleep(0.3)
        menu = read_menu(case)
    return menu


def guest_seconds(case: Case) -> float:
    code, body = case.http("/status")
    return float(json.loads(body).get("guest_time_s", 0.0)) if code == 200 \
        else 0.0


def dismiss_level_up(case: Case) -> None:
    """The level-up popup an XP grant raises asks how to spend the points; A
    keeps them for the player."""
    # Bounded in guest time: under load the guest runs far slower than the
    # wall clock.
    start = guest_seconds(case)
    deadline = time.monotonic() + 120
    popup = read_menu(case)
    while popup.get("popup") is not True and time.monotonic() < deadline \
            and guest_seconds(case) - start < 10.0:
        time.sleep(0.3)
        popup = read_menu(case)
    case.check("the XP grant raised the level-up popup",
               popup.get("popup") is True)
    if popup.get("popup") is not True:
        return
    # A press in the popup's first frames is lost (measured: one at its first
    # sighting did nothing, one a second later closed it).
    time.sleep(1.0)
    case.http("/pad?button=a&hold=0.1")
    closed = wait_game_menu(case, lambda m: m.get("popup") is not True, 10)
    case.check("and A left the points for the player",
               closed.get("popup") is not True)


def read_menu_items(case: Case) -> dict:
    """The menu with every item, as GET /menu?items=all reads it."""
    code, body = case.http("/menu?items=all")
    if code != 200:
        return {}
    try:
        return json.loads(body)
    except ValueError:
        return {}


def menu_items(case: Case) -> list[dict]:
    """Every item of the menu as GET /menu?items=all reads it."""
    return read_menu_items(case).get("items", [])


def menu_item(case: Case, name: str) -> dict:
    """The menu's item named `name` as GET /menu?items=all reads it."""
    return next((item for item in menu_items(case)
                 if item.get("name") == name), {})


def menu_list(case: Case, name: str = "list") -> dict:
    """The list box of the menu's `name` item, as GET /menu?items=all reads
    it (`list` for the shop and the codex, `text_list` for the region)."""
    return menu_item(case, name).get("list_box", {})


def menu_labels(case: Case) -> dict[str, str]:
    """Every item's displayed text by name, as GET /menu?items=all reads it."""
    return {item["name"]: item["label"] for item in menu_items(case)}


def menu_text(raw: str) -> str:
    """Retail text as a player reads it: style escapes and prompt tokens
    removed, whitespace collapsed."""
    text = re.sub(r"~\d\d|~~|\$MENU_\S*", "", raw).replace("$", "")
    return " ".join(text.split())


def option_tabs(case: Case, pattern: str) -> dict[str, bool]:
    """The game's tabs whose names match `pattern`, each with whether it is
    lit (item flags bit 0); tabs without text are left out."""
    return {item["label"]: bool(item["flags"] & 0x01)
            for item in menu_items(case)
            if re.fullmatch(pattern, item["name"]) and item["label"]}


def tap_tab(case: Case, pattern: str, label: str) -> bool:
    """Tap the finger button over the game's tab named by `pattern` whose
    text is `label`."""
    menu = read_menu_items(case)
    tab = next((item for item in menu.get("items", [])
                if re.fullmatch(pattern, item["name"])
                and item["label"] == label), {})
    return tap_button_edge(case, item_button(menu, tab))


def list_entry(box: dict, label: str) -> int:
    """The index of the list box entry that reads `label`, lower case, or
    -1."""
    for index, entry in enumerate(box.get("entries", [])):
        if menu_text(entry).lower() == label:
            return index
    return -1


def list_cards(case: Case, name: str = "list") -> list[dict]:
    """The list box's touch cards where its last draw put them, each with
    its `tap` and the `entry` it shows."""
    return menu_item(case, name).get("touch_cards", [])


def drag_between(case: Case, start: dict, end: dict) -> None:
    """One finger from tap point `start` to `end`'s height through the
    contact phases, in window fractions."""
    x = start["x"]
    steps = 4
    case.http("/touch?x=%g&y=%g&phase=down" % (x, start["y"]))
    for i in range(1, steps + 1):
        time.sleep(0.1)
        case.http("/touch?x=%g&y=%g&phase=motion"
                  % (x, start["y"] + (end["y"] - start["y"]) * i / steps))
    case.http("/touch?x=%g&y=%g&phase=up" % (x, end["y"]))


def reveal_list_entry(case: Case, entry: int,
                      name: str = "list") -> dict | None:
    """Drag the list's cards until one shows `entry`; that card, or None."""
    for _ in range(12):
        cards = list_cards(case, name)
        card = next((c for c in cards if c["entry"] == entry), None)
        if card is not None or len(cards) < 2:
            return card
        if entry > cards[-1]["entry"]:
            drag_between(case, cards[-1]["tap"], cards[0]["tap"])
        else:
            drag_between(case, cards[0]["tap"], cards[-1]["tap"])
        time.sleep(0.8)
    return None


def tap_list_entry(case: Case, entry: int, name: str = "list") -> bool:
    """Tap the card showing `entry`, dragging it into view first."""
    card = reveal_list_entry(case, entry, name)
    return card is not None and tap_menu_item(case, card)


def wait_list(case: Case, done, name: str = "list",
              timeout: float = 10) -> dict:
    """The list box once `done` holds of it, or the last read."""
    deadline = time.monotonic() + timeout
    box = menu_list(case, name)
    while time.monotonic() < deadline and not done(box):
        time.sleep(0.3)
        box = menu_list(case, name)
    return box


def select_list_entry(case: Case, entry: int, name: str = "list") -> dict:
    """Walk the game's selection to `entry` with one tap on its card unless
    it is already there; the list box after."""
    box = menu_list(case, name)
    if box.get("selected") == entry:
        return box
    tap_list_entry(case, entry, name)
    return wait_list(case, lambda b: b.get("selected") == entry, name)


def wait_settled_menu(case: Case, name: str, timeout: float) -> list[dict]:
    """The items of the game's menu `name` once its lit tabs and list
    entries have held still for a second: a shop or stash opens on its first
    tab and the game may step it on a later frame."""
    def state(items: list[dict]):
        return [(i["name"], i["flags"] & 0x01,
                 i.get("list_box", {}).get("entries")) for i in items]

    wait_game_menu(case, lambda m: m.get("menu") == name, timeout)
    items = menu_items(case)
    deadline = time.monotonic() + timeout
    still_since = time.monotonic()
    while time.monotonic() < deadline and time.monotonic() - still_since < 1.0:
        time.sleep(0.2)
        now = menu_items(case)
        if state(now) != state(items):
            still_since = time.monotonic()
        items = now
    return items
