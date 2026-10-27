"""Run the product unattended through every menu its console can open, then
print the owners' run-end reports.

    uv run --frozen python tools/scan.py [--port N]

Each state is reached by a title-owned route: `boot.mode=menu` for the title
menu, `X2_BOOT_MAP` for a level, and the console's `openmenu <name>` for each
menu. No input is scripted. Touch play is forced on so the touch owners run.
"""

from __future__ import annotations

import argparse
import sys
import time
import urllib.parse
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

from live_harness import DEFAULT_PORT, Case
from live_menu import read_menu

LEVEL = "act2/jungle/jungle1"
FRONT_END_MENUS = ("options", "options_controller")
LEVEL_MENUS = ("pda", "options", "options_controller", "team", "shop", "stash", "codex", "worldmap", "review",
               "region", "danger_room", "automap", "trivia")
REPORT_PREFIX = "menu touch gap:"
IDLE_SECONDS = 3.0


def wait_menu_named(case: Case, name: str, timeout: float) -> bool:
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        if read_menu(case).get("menu") == name:
            return True
        time.sleep(0.5)
    return False


def wait_gameplay(case: Case, timeout: float) -> bool:
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        if read_menu(case).get("active") is False and case.status_frames() > 0:
            return True
        time.sleep(0.5)
    return False


def open_menus(case: Case, names: tuple[str, ...]) -> list[str]:
    missed = []
    for name in names:
        case.http("/console?command=" + urllib.parse.quote("openmenu " + name))
        if not wait_menu_named(case, name, 15):
            missed.append(name)
            continue
        time.sleep(IDLE_SECONDS)
    return missed


def run_state(name: str, port: int, env: dict[str, str], settle: str,
              menus: tuple[str, ...]) -> tuple[list[str], list[str]]:
    case = Case("scan-" + name, port)
    case.prepare_profile(["boot.mode=menu", "input.touch_controls=2"])
    case.launch(env)
    missed: list[str] = []
    try:
        case.wait_control(120)
        settled = (wait_menu_named(case, settle, 300) if settle
                   else wait_gameplay(case, 300))
        if not settled:
            missed.append(settle)
        else:
            time.sleep(IDLE_SECONDS)
            missed += open_menus(case, menus)
    finally:
        case.shutdown()
    lines = [line.split(REPORT_PREFIX, 1)[1].strip()
             for line in case.log_text().splitlines() if REPORT_PREFIX in line]
    return lines, missed


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", type=int, default=DEFAULT_PORT)
    args = parser.parse_args()
    states = (
        ("front-end", {}, "main", FRONT_END_MENUS),
        ("level", {"X2_BOOT_MAP": LEVEL}, "", LEVEL_MENUS),
    )
    for name, env, settle, menus in states:
        lines, missed = run_state(name, args.port, env, settle, menus)
        print("== %s" % name)
        for line in lines:
            print("  " + line)
        for menu in missed:
            print("  not reached: %s" % (menu or "gameplay"))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
