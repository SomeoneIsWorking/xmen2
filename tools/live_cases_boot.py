"""Live cases for booting, cutscenes and saves."""

from __future__ import annotations

import re
import time

from live_harness import Case, TUTORIAL_MAP
from live_game import (
    reach_authored_conversation,
    reach_camera_only_lock,
    wait_controls_unlocked,
)


def dialogue_skip_counts(
    report: str,
) -> tuple[int, int, int, int, int, int] | None:
    match = re.search(
        r"dialogue presentation: (\d+) ordinary response, (\d+) ordinary "
        r"line start\(s\); skip stopped (\d+) active voice\(s\), suppressed "
        r"(\d+) response and (\d+) line start\(s\), leaked (\d+);",
        report,
    )
    if not match:
        return None
    return tuple(int(value) for value in match.groups())


def script_sound_counts(report: str) -> tuple[int, int, int] | None:
    match = re.search(
        r"script sound commands: (\d+) ordinary, (\d+) silent; "
        r"last context 0x([0-9a-fA-F]+)",
        report,
    )
    if not match:
        return None
    ordinary, silent, context = match.groups()
    return int(ordinary), int(silent), int(context, 16)


def case_cutscene_skip(case: Case) -> None:
    """Escape completes the tutorial's BehavEd control-lock epoch in one
    player invocation without advancing the guest frame or clock."""
    case.prepare_profile(["boot.mode=normal"])
    case.launch({
        "X2_BOOT_MAP": TUTORIAL_MAP,
        "X2_SCRIPTS": "1",
    })
    case.wait_control(60)

    reached = reach_authored_conversation(case, 240)
    case.check("an authored visible conversation was reached", reached)
    if not reached:
        return
    print(case.get_text("/input?controller=0"))
    case.shot("before-escape")

    code, body = case.http("/key?name=Escape&hold=0.4")
    case.check("Escape accepted by the guest poll", code == 200,
               body.decode(errors="replace").strip())

    case.check("cleanup script nightcrawler_spawn launched",
               case.wait_log('nightcrawler_spawn', 180))
    case.check("conversation-end script conv_0020b_end launched",
               case.wait_log('conv_0020b_end', 180))

    adjacent_ok, evidence = False, ""
    for line in case.log_text().splitlines():
        if 'conversation start "' in line and "0020b" in line:
            evidence = line.strip()
            adjacent_ok = "STARTED" in line and "-> STARTED" in line \
                and "-> 0x00000000)" not in line
    case.check("adjacent conversation started with a visible line "
               "(issue #83 signature absent)", adjacent_ok, evidence)

    unlocked = wait_controls_unlocked(case, 120)
    case.check("controls unlocked after the skip", unlocked)
    report = case.get_text("/input?controller=0")
    (case.dir / "final-input-report.txt").write_text(report)
    player_lines = [line.strip() for line in report.splitlines()
                    if "policy:" in line or "one-step invariant:" in line
                    or "Cutscene player:" in line]
    evidence = "; ".join(player_lines)
    case.check("one request completed in one cutscene-player invocation",
               "1 request(s), 1 invocation(s), 1 completion(s)" in report,
               evidence)
    case.check("the invocation preserved the guest frame and clock",
               "1 same-frame, 1 same-guest-time" in report, evidence)
    case.check("the cutscene-player epoch retired",
               "Cutscene player: active 0" in report, evidence)
    dialogue = dialogue_skip_counts(report)
    case.check("skip stopped the active line and suppressed every later "
               "dialogue presentation",
               dialogue is not None and dialogue[2] > 0 and
               dialogue[3] > 0 and dialogue[4] > 0 and dialogue[5] == 0,
               "dialogue counters %s" % (dialogue,))
    sounds = script_sound_counts(report)
    case.check("both authored sound commands were consumed silently by their "
               "owned BehavEd context",
               sounds is not None and sounds[0] == 0 and sounds[1] == 2 and
               sounds[2] != 0,
               "script sound counters %s" % (sounds,))
    case.shot("after-skip")


def case_cutscene_skip_early(case: Case) -> None:
    """Escape during the CAMERA-ONLY opening stretch skips the whole authored
    sequence, not just whichever record happens to be on screen. The press
    lands while the BehavEd control-lock epoch has no conversation payload,
    which proves the player rather than the conversation owns the operation."""
    case.prepare_profile(["boot.mode=normal"])
    case.launch({
        "X2_BOOT_MAP": TUTORIAL_MAP,
        "X2_SCRIPTS": "1",
    })
    case.wait_control(60)

    report = reach_camera_only_lock(case, 240)
    case.check("a camera-only locked stretch was reached (no record visible)",
               bool(report),
               next((line.strip() for line in report.splitlines()
                     if "boundary:" in line),
                    "controls never locked with nothing visible"))
    if not report:
        return
    visible_before = sum('conversation start "' in line
                         for line in case.log_text().splitlines())
    case.shot("before-escape")

    code, body = case.http("/key?name=Escape&hold=0.4")
    case.check("Escape accepted by the guest poll", code == 200,
               body.decode(errors="replace").strip())

    # The player request must happen here. Outcome alone is not a falsifier:
    # an ignored press would still let retail eventually finish the scene.
    invoked, status = False, ""
    deadline = time.monotonic() + 60
    while time.monotonic() < deadline and not invoked:
        if not case.alive():
            break
        for line in case.get_text("/input?controller=0").splitlines():
            if "policy:" in line:
                status = line.strip()
                invoked = "1 request(s), 1 invocation(s)" in status
        if not invoked:
            time.sleep(1.0)
    case.check("the camera-only Escape invoked the cutscene player",
               invoked, status)

    case.check("conversation-end script conv_0020b_end launched",
               case.wait_log("conv_0020b_end", 240))
    unlocked = wait_controls_unlocked(case, 180)
    case.check("controls unlocked after the skip", unlocked)

    report = case.get_text("/input?controller=0")
    (case.dir / "final-input-report.txt").write_text(report)
    player_lines = [line.strip() for line in report.splitlines()
                    if "policy:" in line or "one-step invariant:" in line
                    or "Cutscene player:" in line]
    status = "; ".join(player_lines)
    case.check("the player completed once and retired its epoch",
               "1 request(s), 1 invocation(s), 1 completion(s)" in report
               and "Cutscene player: active 0" in report, status)
    case.check("camera-only completion preserved the guest frame and clock",
               "1 same-frame, 1 same-guest-time" in report, status)
    dialogue = dialogue_skip_counts(report)
    case.check("camera-only skip suppressed every authored dialogue "
               "presentation",
               dialogue is not None and dialogue[3] > 0 and
               dialogue[4] > 0 and dialogue[5] == 0,
               "dialogue counters %s" % (dialogue,))
    sounds = script_sound_counts(report)
    case.check("camera-only skip consumed both authored sound commands "
               "silently",
               sounds is not None and sounds[0] == 0 and sounds[1] == 2 and
               sounds[2] != 0,
               "script sound counters %s" % (sounds,))
    started = sum('conversation start "' in line
                  for line in case.log_text().splitlines())
    case.check("the sequence's remaining records were consumed, not left "
               "for the player", started > visible_before,
               "%d conversation start(s) before the press, %d after"
               % (visible_before, started))
    case.shot("after-skip")


def case_boot_continue(case: Case) -> None:
    """Boot=Continue reaches the saved map with no menu interaction: the
    title-screen player selection is supplied programmatically and retail
    Continue dispatches synchronously from the intercepted CMenuMain::Show."""
    case.prepare_profile(["boot.mode=continue"])
    case.seed_save("autosave.save")
    case.launch({
        "X2_UNPACED": "1",
        "X2_FILES": "1",
        "X2_SCRIPTS": "1",
    })
    case.wait_control(60)

    case.check("BOOT MODE announced", case.wait_log("BOOT MODE:", 120))
    case.check("player selection supplied without title input",
               case.wait_log("BOOT PLAYER:", 240))
    refused = "refused mode3/state1c" in case.log_text()
    case.check("retail mode-3 save chain did not refuse the dispatch",
               not refused)

    dest_map, main_back_opened = "", False
    deadline = time.monotonic() + 300
    while time.monotonic() < deadline and not dest_map:
        if not case.alive():
            break
        for line in case.log_text().splitlines():
            if "[FILE]" not in line or ".pkgb" not in line:
                continue
            if "menu/main_back" in line:
                main_back_opened = True
            elif "/maps/" in line and "/package/" not in line \
                    and "/menus/" not in line:
                dest_map = line.strip()
        time.sleep(1.0)
    # The direct dispatch is the whole point of the feature: the save chain
    # runs at the intercepted intro command, so the splash wait, the menu map
    # and the menu never happen. Two independent shapes of the fallback are
    # caught here -- the announcement (the runtime says which path it took)
    # and the menu map's own FILE open (the fallback cannot reach the save
    # without loading menu/main_back). A build that fell back fails both.
    log = case.log_text()
    case.check("the splash wait was skipped at the intro phase itself",
               "BOOT SPLASH: intro phase start stamp" in log,
               next((line.strip() for line in log.splitlines()
                     if "BOOT SPLASH:" in line), "no BOOT SPLASH line"))
    case.check("the save chain was dispatched directly at the intro command",
               "dispatching the retail save chain" in log
               and "refused the direct dispatch" not in log)
    case.check("the menu map was never loaded", not main_back_opened,
               "menu/main_back opened" if main_back_opened else
               "no menu/main_back open in %d [FILE] line(s)"
               % sum("[FILE]" in line for line in log.splitlines()))
    case.check("a non-menu destination map opened after the dispatch",
               bool(dest_map), dest_map[:160])
    roster = ("Wolverine", "Cyclops", "Storm", "Magneto", "Nightcrawler",
              "Iceman", "Jean Grey", "Rogue", "Beast", "Gambit", "Jubilee",
              "Colossus", "Psylocke", "Angel", "Deadpool", "Sabretooth")
    party = []
    deadline = time.monotonic() + 60
    while time.monotonic() < deadline and len(party) < 3:
        if not case.alive():
            break
        text = case.log_text()
        party = [name for name in roster
                 if "characters/%s" % name in text]
        time.sleep(2.0)
    case.check("a party spawned in the destination map", len(party) >= 3,
               ", ".join(party))

    # The loaded save replays the level's authored intro conversations, and
    # Continue does NOT touch them: it loads the save and nothing else. So
    # this case has to advance the scene the way a player does -- the
    # authored-skip Escape -- BEFORE the adjacent conversation can be
    # expected. It used to assert 0020b first and press Escape afterwards,
    # which only ever passed because the removed boot-Continue auto-resume
    # advanced the records and floor-clamped the script waits with no input
    # at all. With retail pacing restored, 0020b arrives tens of thousands of
    # frames later, on the far side of the press.
    if not wait_controls_unlocked(case, 20):
        code, body = case.http("/key?name=Escape&hold=0.4")
        case.check("Escape accepted on the resumed conversation", code == 200,
                   body.decode(errors="replace").strip())
    # With no resolved hero the second conversation collides on the seen-line
    # bitmap and never shows a line (issue #83), which is the reported
    # Continue softlock; with the player resolved it plays and hands controls
    # back.
    case.check("the adjacent conversation started with a line",
               case.wait_log('1_introlevel_0020b" -> STARTED', 240))
    line_ok = any('0020b" -> STARTED' in line and "0x13" in line
                  for line in case.log_text().splitlines())
    case.check("0020b is visible with a selected line (no seen-bit "
               "collision)", line_ok)
    # The end script's FILE open is a level-load preload, so the LAUNCH line
    # is the completion evidence.
    case.check("conversation-end cleanup launched",
               case.wait_log('SCRIPT: launch "act0/tutorial/tutorial1/'
                             'conv_0020b_end"', 240))
    unlocked = wait_controls_unlocked(case, 240)
    case.check("controls unlocked after the resumed conversations", unlocked)
    report = case.get_text("/input?controller=0")
    (case.dir / "final-input-report.txt").write_text(report)
    handles = [line.strip() for line in report.splitlines()
               if ("player " in line and "actor" in line)
               or "current player" in line]
    case.check("current player resolved like manual Continue",
               any("current player index 0" in line for line in handles)
               and sum("UNRESOLVED" not in line for line in handles) >= 1,
               " | ".join(handles))
    case.shot("gameplay")
    case.shot("after-continue")


def case_manual_continue(case: Case) -> None:
    """CONTROL for boot-continue: drive the retail main menu with Return and
    Continue from the click path, then compare the loaded game's hero
    handles with what the bypassing boot produces."""
    case.prepare_profile(["boot.mode=normal"])
    case.seed_save("autosave.save")
    case.launch({
        "X2_UNPACED": "1",
        "X2_FILES": "1",
        "X2_SCRIPTS": "1",
    })
    case.wait_control(60)
    case.check("main-menu map lifecycle opened",
               case.wait_log("menus/main.pkgb", 300))
    time.sleep(12)
    case.shot("menu")
    code, body = case.http("/key?name=Return&hold=0.4")
    case.check("Return accepted on the menu", code == 200,
               body.decode(errors="replace").strip())
    case.check("destination map opened",
               case.wait_log("/maps/act0/tutorial/tutorial1.pkgb", 300))
    time.sleep(25)
    case.shot("after-manual-continue")
    report = case.get_text("/input?controller=0")
    (case.dir / "final-input-report.txt").write_text(report)
    handles = [line.strip() for line in report.splitlines()
               if ("player " in line and "actor" in line)
               or "current player" in line]
    resolved = sum("UNRESOLVED" not in line for line in handles)
    case.check("hero handles resolved after manual Continue",
               resolved >= 1, " | ".join(handles))
