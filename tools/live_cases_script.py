"""Multi-statement scripts through GET /script, in gameplay.

The retail loader splits a script file on CRLF only, so a script whose lines
reached it with bare LF breaks ran as one statement and overflowed the parse
node (docs/RE/console.md). Two scripts in one run also prove each new file is
found: the second one is opened after the overlay directory was listed.
"""
from __future__ import annotations

import json
import time
import urllib.parse

from live_harness import Case
from live_menu import keyboard_into_gameplay

HEROES = ("_HERO1_", "_HERO2_", "_HERO3_", "_HERO4_")


def read_party(case: Case) -> dict:
    return json.loads(case.get_text("/party") or "{}")


def hero_xs(party: dict) -> list[float]:
    return [hero["x"] for hero in party.get("heroes", [])]


def run_script(case: Case, lines: list[str]) -> bool:
    status, reply = case.http(
        "/script?text=" + urllib.parse.quote("\n".join(lines)))
    case.check("/script took %d statements" % len(lines), status == 200,
               reply.decode(errors="replace").strip())
    return status == 200


def wait_party(case: Case, done, timeout: float = 10) -> dict:
    deadline = time.monotonic() + timeout
    party = read_party(case)
    while time.monotonic() < deadline and not done(party):
        time.sleep(0.3)
        party = read_party(case)
    return party


def case_script_lines(case: Case) -> None:
    case.prepare_profile(["boot.mode=continue"])
    case.seed_save("autosave0.save")
    case.launch({"X2_FILES": "1"})
    case.wait_control(60)
    menu = keyboard_into_gameplay(case, 300)
    case.check("Continue reached gameplay", menu.get("active") is False,
               str(menu.get("menu")))
    start = read_party(case)
    case.check("the party has four heroes", len(hero_xs(start)) == 4,
               str(hero_xs(start)))
    if len(hero_xs(start)) != 4 or start.get("money") is None:
        return

    x = round(hero_xs(start)[0]) + 40
    if run_script(case, ["setPosX('%s',%d)" % (hero, x) for hero in HEROES]):
        moved = wait_party(case, lambda p: hero_xs(p) == [x] * 4)
        case.check("four statements moved every hero to x=%d" % x,
                   hero_xs(moved) == [x] * 4, str(hero_xs(moved)))
    case.check("the run survived four statements", case.alive())
    if not case.alive():
        return

    # Each line reads the previous one's result, so the total proves all
    # twelve ran in order.
    steps = range(1, 11)
    twelve = ['money = getInventoryCount("MONEY" )']
    twelve += ["money = iadd(money, %d )" % step for step in steps]
    twelve += ['setInventoryCount("MONEY", money )']
    before = read_party(case)["money"]
    wanted = before + sum(steps)
    if run_script(case, twelve):
        after = wait_party(case, lambda p: p.get("money") == wanted)
        case.check("twelve statements took money %d -> %d" % (before, wanted),
                   after.get("money") == wanted, str(after.get("money")))
    case.check("the run survived twelve statements", case.alive())
    case.check("the guest opened both scripts from the overlay",
               all('REPLACED "scripts/x2/control/%d.py"' % n
                   in case.log_text() for n in (1, 2)))
