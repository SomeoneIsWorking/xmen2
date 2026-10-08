"""Combat benchmark: the retail autosave's party fights Lady Deathstrike.

The Danger Room boss room is a fight a few steps from the spawn point, so a
fixed walk reaches it without a route script. Frame times are printed as
observations; the check is structural: a fight translates its working set
once, so the JIT must not flush its whole cache during the run (issue #194).
"""
from __future__ import annotations

import json
import re
import time

from live_harness import Case
from live_menu import keyboard_into_gameplay

ARENA = "dr/dr_boss1"
WALK_STEPS = 6
FIGHT_SECONDS = 18.0
JIT_SUMMARY = re.compile(
    r"\] JIT: \d+ block\(s\) entered \((\d+) translated, .*?(\d+) flush\(es\)")


def party_health(case: Case) -> dict[str, float]:
    party = json.loads(case.get_text("/party") or "{}")
    return {hero["definition"]: hero["health"]
            for hero in party.get("heroes", [])}


def case_combat_bench(case: Case) -> None:
    case.prepare_profile(["boot.mode=continue"])
    case.seed_save("autosave.save")
    case.launch({})
    case.wait_control(60)
    menu = keyboard_into_gameplay(case, 300)
    case.check("Continue reached gameplay", menu.get("active") is False,
               str(menu.get("menu")))
    case.http("/console?command=loadmap%20" + ARENA)
    time.sleep(8)
    menu = keyboard_into_gameplay(case, 300)
    case.check("loadmap %s reached gameplay" % ARENA,
               menu.get("active") is False, str(menu.get("menu")))
    time.sleep(4)
    before = party_health(case)
    case.http("/performance/reset")
    for _ in range(WALK_STEPS):
        case.http("/key?name=w&hold=1.5")
        time.sleep(1.7)
    time.sleep(FIGHT_SECONDS)
    after = party_health(case)
    case.shot("fight")
    hurt = [name for name, health in after.items()
            if health < before.get(name, health)]
    case.check("the boss fight damaged the party", bool(hurt), str(after))
    status = json.loads(case.get_text("/status") or "{}")
    print("  frames: p50 %s ms, p95 %s ms, p99 %s ms, max %s ms over %s"
          % (status.get("frame_ms_p50"), status.get("frame_ms_p95"),
             status.get("frame_ms_p99"), status.get("frame_ms_max"),
             status.get("frame_sample_count")))
    case.shutdown()
    summary = JIT_SUMMARY.search(case.log_text())
    case.check("the engine printed its JIT summary", summary is not None)
    if summary:
        case.check("the JIT never flushed its whole cache",
                   summary.group(2) == "0",
                   "%s flush(es), %s block(s) translated"
                   % (summary.group(2), summary.group(1)))
