"""Drops and their models: enemies killed beside the party drop items, and
`/pickups` says which of them has a loaded model (external/xmen2-decomp/docs/notes/pickups.md).

`pickup-models` is the shipped data: every drop is loaded and nothing is
logged. `pickup-models-unloadable` makes the money models unloadable through
the run's asset overlay, so a money drop takes setModel's file-load refusal
and keeps its prompt with no model, which setItem's report names in the log.
"""
from __future__ import annotations

import json
import time
import urllib.parse

from live_harness import Case
from live_menu import keyboard_into_gameplay

LEVEL = "act1/deadzone/deadzone2"
ENEMY = "_ms_ss_mikhailranger_a"
ENEMIES = 6
# Drops are random; kill waves until one drops (money, when unloadable).
WAVES = 6
UNLOADABLE = ("tech_01", "tech_02", "tech_03")


def pickups(case: Case) -> dict:
    return json.loads(case.get_text("/pickups") or "{}")


def run_script(case: Case, code: str) -> None:
    status, reply = case.http("/script?text=" + urllib.parse.quote(code))
    if status != 200:
        print("  /script %d: %s" % (status, reply.decode(errors="replace")))
    time.sleep(0.4)


def kill_for_drops(case: Case, unloadable: bool) -> None:
    case.prepare_profile(["boot.mode=continue"])
    case.seed_save("autosave0.save")
    if unloadable:
        models = case.dir / "overlay" / "models" / "pickups"
        models.mkdir(parents=True)
        for name in UNLOADABLE:
            (models / (name + ".igb")).write_bytes(b"")
    case.launch({})
    case.wait_control(60)
    menu = keyboard_into_gameplay(case, 300)
    case.check("Continue reached gameplay", menu.get("active") is False,
               str(menu.get("menu")))
    case.http("/console?command=" + urllib.parse.quote("loadmap " + LEVEL))
    time.sleep(8)
    menu = keyboard_into_gameplay(case, 300)
    case.check("loadmap %s reached gameplay" % LEVEL,
               menu.get("active") is False, str(menu.get("menu")))
    before = {p["handle"] for p in pickups(case).get("pickups", [])}
    state: dict = {}
    drops: list[dict] = []
    for wave in range(WAVES):
        for index in range(ENEMIES):
            run_script(case, "spawn('_ACTIVE_HERO_','%s','x2_drop%d_%d',"
                       "' %d 0 0 ',' 0 0 0 ')"
                       % (ENEMY, wave, index, 120 + 30 * index))
        time.sleep(1.0)
        for index in range(ENEMIES):
            run_script(case, "killEntity('x2_drop%d_%d')" % (wave, index))
        time.sleep(6.0)
        state = pickups(case)
        drops = [p for p in state.get("pickups", [])
                 if p["handle"] not in before]
        if any(not unloadable or p["definition"] == "inventory_money"
               for p in drops):
            break
    case.shot("drops")
    print("  /pickups: " + json.dumps(state))
    for drop in drops:
        print("  drop %d %s %r loaded=%s" % (drop["handle"], drop["definition"],
                                             drop["label"],
                                             drop["model"]["loaded"]))
    case.check("the killed enemies dropped items", bool(drops),
               "%d drop(s)" % len(drops))
    money = [p for p in drops if p["definition"] == "inventory_money"]
    undrawn = money if unloadable else []
    drawn = [p for p in drops if p not in undrawn]
    case.check("every drop with a loadable model has it loaded",
               all(p["model"]["loaded"] for p in drawn),
               "%d drop(s)" % len(drawn))
    case.check("without_model counts the drops listed without one",
               state.get("without_model") == len(undrawn),
               "%s" % state.get("without_model"))
    reports = [line for line in case.log_text().splitlines()
               if "setItem 0x00479cc0: pickup" in line]
    for line in reports[:4]:
        print("  " + line)
    if unloadable:
        case.check("every money drop is listed without a model",
                   bool(money) and not any(p["model"]["loaded"]
                                           for p in money),
                   "%d money drop(s)" % len(money))
        case.check("each money drop's refusal was logged as it dropped",
                   len(reports) == len(money)
                   and all("the model file did not load" in line
                           for line in reports),
                   "%d report(s)" % len(reports))
    else:
        case.check("no drop was reported without a model", not reports,
                   "%d report(s)" % len(reports))
    case.shutdown()


def case_pickup_models(case: Case) -> None:
    kill_for_drops(case, unloadable=False)


def case_pickup_models_unloadable(case: Case) -> None:
    kill_for_drops(case, unloadable=True)
