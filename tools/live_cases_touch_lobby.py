"""Live cases for the online lobby menus native on touch (docs/touch-play.md).

Each boots to the title menu with touch forced on and walks retail's own
front-end path by finger: the main menu's play online, the player setup's
Ready, the campaign lobby's Host Game or Join Game, and the screens they
open (src/native/lan_session_director.cpp names the same path). It judges by
GET /menu: every row and footer a finger button in the window, and each tap
opening the menu retail opens.
"""

from __future__ import annotations

import json
import time
import urllib.parse

from live_cases_touch_map import inside_window
from live_cases_touch_pages import check_finger_buttons, shapes
from live_harness import Case
from live_menu import (finger_tall, footer_button, item_button, read_menu,
                       read_menu_items, tap_button_edge, wait_menu)


def into_title(case: Case, width: int, height: int) -> bool:
    """Booted to the title menu in a width x height window."""
    case.prepare_profile(["boot.mode=menu", "input.touch_controls=2",
                          "video.width=%d" % width,
                          "video.height=%d" % height,
                          "video.mode=windowed"])
    case.launch({})
    case.wait_control(120)
    return wait_menu(case, "main", 300).get("menu") == "main"


def settled(case: Case, name: str, timeout: float = 20) -> dict:
    """The menu `name` once its finger buttons stop moving in."""
    menu = wait_menu(case, name, timeout)
    if menu.get("menu") != name:
        return menu
    last = None
    deadline = time.monotonic() + 10
    while time.monotonic() < deadline:
        menu = read_menu_items(case)
        shown = [(b.get("edge"), b.get("fingers"))
                 for b in menu.get("touch_buttons", [])]
        if shown and shown == last:
            break
        last = shown
        time.sleep(0.5)
    return menu


def lobby_buttons(menu: dict) -> dict[str, dict]:
    """Each row and text item by its lower-case label, with the finger
    button over it: the lobby's rows are not linked as the menu's rows."""
    return {item["label"].lower(): item_button(menu, item)
            for item in menu.get("texts", []) + menu.get("rows", [])
            if item.get("label")}


def check_screen(case: Case, menu: dict, name: str, menu_class: str,
                 rows: list[str]) -> bool:
    """`name` is up as `menu_class`, no pad over it, its `rows` and footers
    finger buttons in the window."""
    (case.dir / ("%s.json" % name)).write_text(json.dumps(menu, indent=1))
    up = menu.get("menu") == name and menu.get("class") == menu_class
    case.check("%s is up as %s" % (name, menu_class), up,
               "%s %s" % (menu.get("menu"), menu.get("class")))
    if not up:
        case.shot("no-" + name)
        return False
    case.shot(name)
    check_finger_buttons(case, menu, name)
    buttons = lobby_buttons(menu)
    for row in rows:
        button = buttons.get(row, {})
        case.check("%s: %s is a finger button in the window" % (name, row),
                   finger_tall(button) and inside_window(button), str(button))
    return True


def tap_row_to(case: Case, name: str, row: str, opens: str) -> dict:
    """Tap `row` on `name` at its edge; the menu it leaves up."""
    tap_button_edge(case, lobby_buttons(read_menu_items(case)).get(row, {}))
    menu = settled(case, opens)
    case.check("%s: a tap on %s opens %s" % (name, row, opens),
               menu.get("menu") == opens, str(menu.get("menu")))
    return menu


def tap_back_to(case: Case, name: str, opens: str) -> dict:
    tap_button_edge(case, footer_button(read_menu(case), "$MENU_BACK"))
    menu = settled(case, opens)
    case.check("%s: a tap on Back returns to %s" % (name, opens),
               menu.get("menu") == opens, str(menu.get("menu")))
    return menu


def into_lobby(case: Case, width: int, height: int) -> dict:
    """The title's play online, then the player setup's Ready: the campaign
    lobby."""
    case.check("the run reached the title menu",
               into_title(case, width, height))
    time.sleep(1.0)
    menu = tap_row_to(case, "main", "play online", "online")
    if not check_screen(case, menu, "online", "CMenuOnline",
                        ["input name", "ready"]):
        return menu
    menu = tap_row_to(case, "online", "ready", "campaign_lobby")
    check_screen(case, menu, "campaign_lobby", "CMenuCampaignLobby",
                 ["host game", "join game"])
    return menu


HOST_SHAPES = shapes("touch-lobby-host")


def case_touch_lobby_host(case: Case) -> None:
    """Host Game by finger: the game options' rows and Post Game, the host
    room's Start Game, Kick Player and Leave Room, and Leave Room back to the
    game options."""
    if into_lobby(case, *HOST_SHAPES[case.name]).get("menu") != "campaign_lobby":
        return
    menu = tap_row_to(case, "campaign_lobby", "host game", "game_options")
    if not check_screen(case, menu, "game_options", "CMenuGameOptions",
                        ["game name", "difficulty", "game type",
                         "max. players", "post game"]):
        return
    menu = tap_row_to(case, "game_options", "post game", "host")
    if not check_screen(case, menu, "host", "CMenuHost",
                        ["start game", "kick player", "leave room"]):
        return
    tap_row_to(case, "host", "leave room", "game_options")
    case.shot("after")


JOIN_SHAPES = shapes("touch-lobby-join")


def case_touch_lobby_join(case: Case) -> None:
    """Join Game by finger: the join options' rows and Search, the games
    list and its Back; then the players list and the join room, which only
    the console opens with no session. With no session the join room raises
    "no longer available" over its own rows and closes with it, so its rows
    are judged under the popup and the popup's button is tapped to close
    both."""
    if into_lobby(case, *JOIN_SHAPES[case.name]).get("menu") != "campaign_lobby":
        return
    menu = tap_row_to(case, "campaign_lobby", "join game",
                      "player_game_options")
    if not check_screen(case, menu, "player_game_options",
                        "CMenuPlayerGameOptions", ["search"]):
        return
    menu = tap_row_to(case, "player_game_options", "search", "games_list")
    if not check_screen(case, menu, "games_list", "CMenuGamesList", []):
        return
    tap_back_to(case, "games_list", "player_game_options")
    case.http("/console?command=" + urllib.parse.quote("openmenu players_list"))
    menu = settled(case, "players_list")
    check_screen(case, menu, "players_list", "CMenuPlayersList", [])
    case.http("/console?command=" + urllib.parse.quote("openmenu join"))
    menu = settled(case, "join")
    case.check("the join room raises its popup", bool(menu.get("popup")),
               str(menu.get("popup")))
    check_screen(case, menu, "join", "CMenuJoin", ["ready", "leave room"])
    footers = (menu.get("popup_page") or {}).get("footers", [])
    if footers:
        case.http("/touch?x=%g&y=%g" % (footers[0]["tap"]["x"],
                                        footers[0]["tap"]["y"]))
    menu = settled(case, "player_game_options")
    case.check("a tap on the popup's button closes it and the join room",
               menu.get("menu") == "player_game_options"
               and menu.get("popup") is False,
               "%s popup=%s" % (menu.get("menu"), menu.get("popup")))
    case.shot("after")


LOBBY_CASES = {name: case_touch_lobby_host for name in HOST_SHAPES}
LOBBY_CASES.update({name: case_touch_lobby_join for name in JOIN_SHAPES})
