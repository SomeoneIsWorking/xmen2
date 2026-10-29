"""Run the product unattended through every menu its console can open, then
print the owners' run-end reports and every menu the menu pad is drawn over.

    uv run --frozen python tools/scan.py [--port N] [--binary PATH]

Each state is reached by a title-owned route: `boot.mode=menu` for the title
menu, `X2_BOOT_MAP` for a level, and the console's `openmenu <name>` for each
menu. The only input is a tap answering the level's opening line, which
otherwise stays up under every menu and holds the pad off them. Touch play is
forced on so the touch owners run.
"""

from __future__ import annotations

import argparse
import sys
import time
import urllib.parse
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

from live_cases_touch_map import LEVEL, past_opening_line
from live_game import live_controls
from live_harness import BINARY, DEFAULT_PORT, Case, RunOptions
from live_menu import read_menu

# The online lobby's screens, in the order retail's front end opens them
# (src/native/lan_session_director.cpp); join last, since with no session
# its popup stays up over whatever opens after it.
LOBBY_MENUS = ("online", "campaign_lobby", "game_options", "host", "player_game_options", "games_list",
               "players_list", "join")
# Advanced Options: `openmenu sebas` builds a plain CMenu, so the scan reaches
# the real one as retail does, Start on Options (CMenuOptions' update). It
# goes last in the level: every menu opened over it is built as a plain
# CMenu, and the front end ends on the join room's popup.
SEBAS = "options+ok"
FRONT_END_MENUS = ("options", "options_controller", *LOBBY_MENUS)
LEVEL_MENUS = ("pda", "options", "options_controller", "team", "shop", "stash", "codex", "worldmap", "review",
               "region", "danger_room", "automap", "trivia", "image_viewer", "credits", "personal", SEBAS)
REPORT_PREFIX = "menu touch gap:"
IDLE_SECONDS = 3.0


def wait_menu_named(case: Case, name: str, timeout: float) -> bool:
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        if read_menu(case).get("menu") == name:
            return True
        time.sleep(0.5)
    return False


def open_menu(case: Case, name: str) -> str:
    """Open `name` by its route; the menu name it should show."""
    if name == SEBAS:
        case.http("/console?command=" + urllib.parse.quote("openmenu options"))
        if wait_menu_named(case, "options", 15):
            time.sleep(IDLE_SECONDS)
            case.http("/menu/action?name=ok")
        return "sebas"
    case.http("/console?command=" + urllib.parse.quote("openmenu " + name))
    return name


def padded(case: Case) -> bool:
    """Whether the menu pad is drawn right now."""
    return any(name.startswith("menu-") for name in live_controls(case))


def open_menus(case: Case, names: tuple[str, ...]) -> tuple[list[str], list[str]]:
    missed, pads = [], []
    for name in names:
        shown = open_menu(case, name)
        if not wait_menu_named(case, shown, 15):
            missed.append(name)
            continue
        time.sleep(IDLE_SECONDS)
        if padded(case):
            menu = read_menu(case)
            pads.append("%s (%s)" % (shown, menu.get("class")))
    return missed, pads


def run_state(name: str, port: int, env: dict[str, str], settle: str,
              menus: tuple[str, ...],
              options: RunOptions) -> tuple[list[str], list[str], list[str]]:
    case = Case("scan-" + name, port, options)
    case.prepare_profile(["boot.mode=menu", "input.touch_controls=2"])
    case.launch(env)
    missed: list[str] = []
    pads: list[str] = []
    try:
        case.wait_control(120)
        settled = (wait_menu_named(case, settle, 300) if settle
                   else past_opening_line(case, 300))
        if not settled:
            missed.append(settle)
        else:
            time.sleep(IDLE_SECONDS)
            unreached, pads = open_menus(case, menus)
            missed += unreached
    finally:
        case.shutdown()
    lines = [line.split(REPORT_PREFIX, 1)[1].strip()
             for line in case.log_text().splitlines() if REPORT_PREFIX in line]
    return lines, missed, pads


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", type=int, default=DEFAULT_PORT)
    parser.add_argument("--binary", type=Path, default=BINARY)
    args = parser.parse_args()
    options = RunOptions(binary=args.binary)
    states = (
        ("front-end", {}, "main", FRONT_END_MENUS),
        ("level", {"X2_BOOT_MAP": LEVEL}, "", LEVEL_MENUS),
    )
    for name, env, settle, menus in states:
        lines, missed, pads = run_state(name, args.port, env, settle, menus,
                                        options)
        print("== %s" % name)
        for line in lines:
            print("  " + line)
        for menu in pads:
            print("  menu pad drawn over: %s" % menu)
        for menu in missed:
            print("  not reached: %s" % (menu or "gameplay"))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
