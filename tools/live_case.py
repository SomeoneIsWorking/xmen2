#!/usr/bin/env python3
"""Launch and drive a bounded live x2native run for one named scenario.

This is an agent-owned harness. It never invokes run.sh or tools/run.py: it
starts scratch/build-native/x2native DIRECTLY, on an isolated profile, with a
dedicated control port, and talks to that port itself rather than through
scratch/run/live.json.

    tools/live_case.py cutscene-skip          # Escape skips the tutorial scene
    tools/live_case.py cutscene-skip-early    # ... pressed in the camera-only pan
    tools/live_case.py boot-continue          # Boot=Continue reaches the saved map
    tools/live_case.py pad-late               # pad attached after start works
    tools/live_case.py pad-persisted          # stored controller0 id adopted

Every case prints PASS/FAIL evidence lines and exits 0 only on a full pass.
Artifacts (log, screenshots, profile) stay under scratch/run/cases/<case>/.
The run is killed BY PID at the end; nothing is left running.
"""

from __future__ import annotations

import argparse
import json
import os
import re
import shutil
import signal
import subprocess
import sys
import time
import urllib.error
import urllib.request
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
BINARY = ROOT / "scratch" / "build-native" / "x2native"
SELECTED = {"path": BINARY}
CASES_DIR = ROOT / "scratch" / "run" / "cases"
LIVE_JSON = ROOT / "scratch" / "run" / "live.json"
DEFAULT_PORT = 8461
PACING = "fast"
TUTORIAL_MAP = "act0/tutorial/tutorial1"


def refuse(message: str) -> None:
    raise SystemExit("live_case: %s" % message)


def pid_alive(pid: int) -> bool:
    try:
        os.kill(pid, 0)
    except ProcessLookupError:
        return False
    except PermissionError:
        return True
    return True


class Case:
    def __init__(self, name: str, port: int) -> None:
        self.name = name
        self.port = port
        self.dir = CASES_DIR / name
        self.profile = self.dir / "profile"
        self.log_path = self.dir / "run.log"
        self.shot_dir = self.dir / "shots"
        self.proc: subprocess.Popen | None = None
        self.log_file = None
        self.checks: list[tuple[str, bool]] = []
        self.published_snapshot = None

    # -- lifecycle -----------------------------------------------------------

    def prepare_profile(self, conf_lines: list[str]) -> None:
        if CASES_DIR.exists():
            shutil.rmtree(self.dir, ignore_errors=True)
        save_leaf = self.profile / "Activision" / "X-Men Legends 2" / "Save"
        save_leaf.mkdir(parents=True)
        self.shot_dir.mkdir(parents=True)
        conf = self.profile / "x2native.conf"
        conf.write_text(
            "# x2native settings -- written by tools/live_case.py\n"
            + "".join(line + "\n" for line in conf_lines))

    def seed_save(self, leaf: str) -> None:
        src_dir = (ROOT / "scratch" / "saves" / "Activision"
                   / "X-Men Legends 2" / "Save")
        src = src_dir / leaf
        if not src.is_file():
            refuse("no %s under %s to seed the isolated profile with; "
                   "this case needs a real retail save" % (leaf, src_dir))
        shutil.copy2(src, self.profile / "Activision" / "X-Men Legends 2"
                     / "Save" / leaf)

    def launch(self, env_extra: dict[str, str]) -> None:
        binary = SELECTED["path"]
        if not binary.is_file():
            refuse("%s does not exist; build x2native first" % binary)
        if LIVE_JSON.is_file():
            try:
                self.published_snapshot = json.loads(LIVE_JSON.read_text())
            except ValueError:
                self.published_snapshot = None
        if self.published_snapshot and self.published_snapshot.get("running") \
                and pid_alive(self.published_snapshot.get("pid", -1)):
            refuse("scratch/run/live.json names a RUNNING pid %s; refusing to "
                   "clobber another live run's discovery record"
                   % self.published_snapshot.get("pid"))

        env = dict(os.environ)
        env["X2_SAVE_DIR"] = str(self.profile)
        env["SDL_AUDIODRIVER"] = "dummy"
        cmd = [str(binary), "--no-window", "--d3d8",
               "--control=%d" % self.port]
        if PACING in ("uncapped", "fast"):
            env["X2_UNPACED"] = "1"
        if PACING == "fast":
            env["X2_UNBOUNDED"] = "1"
            cmd.append("--unbounded")
        env.update(env_extra)
        self.log_file = self.log_path.open("wb")
        print("launch: %s" % " ".join(cmd))
        for key in sorted(env_extra):
            print("  env %s=%s" % (key, env_extra[key]))
        self.proc = subprocess.Popen(cmd, cwd=ROOT, env=env,
                                     stdout=self.log_file,
                                     stderr=subprocess.STDOUT)

    def shutdown(self) -> None:
        if self.proc and self.proc.poll() is None:
            self.proc.send_signal(signal.SIGTERM)
            try:
                self.proc.wait(timeout=10)
            except subprocess.TimeoutExpired:
                self.proc.kill()
                self.proc.wait(timeout=10)
        if self.log_file:
            self.log_file.close()
            self.log_file = None
        if self.published_snapshot is not None:
            LIVE_JSON.write_text(json.dumps(self.published_snapshot,
                                            indent=2) + "\n")

    # -- driving and reading --------------------------------------------------

    def http(self, path: str, timeout: float = 30.0) -> tuple[int, bytes]:
        url = "http://127.0.0.1:%d%s" % (self.port, path)
        try:
            with urllib.request.urlopen(url, timeout=timeout) as r:
                return r.status, r.read()
        except urllib.error.HTTPError as exc:
            return exc.code, exc.read()
        except OSError:
            return 0, b""

    def get_text(self, path: str) -> str:
        code, body = self.http(path)
        return body.decode(errors="replace") if code == 200 else ""

    def status_frames(self) -> int:
        for line in self.get_text("/status").splitlines():
            parts = line.replace(",", " ").split()
            lowered = [p.lower() for p in parts]
            if "frame" in lowered or "frames" in lowered:
                digits = [int(p) for p in parts if p.isdigit()]
                if digits:
                    return digits[0]
        return -1

    def alive(self) -> bool:
        return self.proc is not None and self.proc.poll() is None

    def wait_control(self, timeout: float) -> None:
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            if not self.alive():
                refuse("the run exited early (rc=%s); log: %s"
                       % (self.proc.returncode, self.log_path))
            try:
                code, _ = self.http("/status", timeout=2.0)
                if code == 200:
                    return
            except OSError:
                time.sleep(0.5)
        refuse("the control channel on port %d never answered within %.0fs"
               % (self.port, timeout))

    def log_text(self) -> str:
        try:
            return self.log_path.read_text(errors="replace")
        except OSError:
            return ""

    def wait_log(self, needle: str, timeout: float) -> bool:
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            if not self.alive():
                return needle in self.log_text()
            if needle in self.log_text():
                return True
            time.sleep(0.5)
        return False

    def shot(self, name: str) -> Path:
        out = self.shot_dir / ("%s.png" % name)
        try:
            code, body = self.http("/screenshot")
        except OSError as exc:
            out.write_text(str(exc))
            return out
        if code == 200:
            out.write_bytes(body)
        else:
            out.write_text(body.decode(errors="replace"))
        return out

    def check(self, what: str, ok: bool, detail: str = "") -> None:
        self.checks.append((what, bool(ok)))
        print("  [%s] %s%s" % ("PASS" if ok else "FAIL", what,
                               (": " + detail) if detail else ""))

    def finish(self) -> int:
        self.shutdown()
        total = len(self.checks)
        good = sum(1 for _, ok in self.checks if ok)
        passed = total and good == total
        print("%s: %d/%d check(s) passed; artifacts in %s"
              % ("PASS" if passed else "FAIL", good, total, self.dir))
        return 0 if passed else 1


# -- cases --------------------------------------------------------------------

def reach_camera_only_lock(case: Case, timeout: float) -> str:
    """Wait for the authored sequence's CAMERA-ONLY stretch: the control lock
    holds and no conversation record is visible. That is the opening pan the
    tutorial plays for seconds before its first line exists -- the stretch a
    player is most likely to press Escape in, and the one the visible-line
    path never covers. Returns the report that proved it, or ""."""
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        if not case.alive():
            return ""
        report = case.get_text("/input?controller=0")
        if "Cutscene player: active 1" in report \
                and "boundary: controls locked; conversation payload inactive" \
                in report:
            return report
        time.sleep(0.5)
    return ""


def reach_authored_conversation(case: Case, timeout: float) -> bool:
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        if not case.alive():
            return False
        report = case.get_text("/input?controller=0")
        if "Cutscene player: active 1" in report \
                and "conversation payload deterministic" in report:
            return True
        time.sleep(1.0)
    return False


def pad_poll_rate(case: Case) -> int:
    """The latest 'the game read a button N time(s)' heartbeat counter."""
    n = 0
    for line in case.log_text().splitlines():
        if "the game read a button" in line:
            digits = [int(t) for t in line.replace(",", " ").split()
                      if t.isdigit()]
            if digits:
                n = digits[0]
    return n


def wait_pad_polled(case: Case, timeout: float) -> bool:
    """Wait until the GAME (not the probe) is polling the pad: the heartbeat
    counter must grow well beyond what this harness's own /input polls add."""
    start = pad_poll_rate(case)
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        if not case.alive():
            return False
        if pad_poll_rate(case) > start + 200:
            return True
        time.sleep(1.0)
    return False


def wait_controls_unlocked(case: Case, timeout: float) -> bool:
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        if not case.alive():
            return False
        report = case.get_text("/input?controller=0")
        if "boundary: controls released; conversation payload inactive" \
                in report:
            return True
        time.sleep(1.0)
    return False


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


def case_pad_late(case: Case) -> None:
    """A controller attached AFTER game start is admitted by the game's own
    enumeration, assignable as Player 1, and its Start acts in gameplay."""
    case.prepare_profile(["boot.mode=normal"])
    case.launch({
        "X2_BOOT_MAP": TUTORIAL_MAP,
        "X2_VIRTUAL_PAD": "f600",
    })
    case.wait_control(60)

    reached = reach_authored_conversation(case, 240)
    case.check("authored conversation reached (gameplay context)", reached)
    if not reached:
        return
    code, body = case.http("/key?name=Escape&hold=0.4")
    case.check("keyboard Escape cleared the authored scene", code == 200,
               body.decode(errors="replace").strip())
    case.check("controls unlocked after the skip",
               wait_controls_unlocked(case, 180))
    case.check("synthetic pad attached late",
               case.wait_log("DINPUT-PAD: pad 0 connected", 60))
    case.check("hotswap re-entered the game's enumeration",
               case.wait_log("DINPUT8: HOTSWAP", 60))
    code, body = case.http("/assignment?player=1&pad=0")
    case.check("pad assigned to player 1 over the control channel",
               code == 200, body.decode(errors="replace").strip())
    time.sleep(1.0)

    before = case.shot("before-start")
    code, body = case.http("/pad?button=start&hold=2.0")
    case.check("Start press delivered to the synthetic pad", code == 200,
               body.decode(errors="replace").strip())

    responded, worst = False, 0.0
    deadline = time.monotonic() + 20
    while time.monotonic() < deadline and not responded:
        current = case.shot("after-start")
        worst = png_mean_diff(before, current)
        responded = worst > 8.0
        time.sleep(1.0)
    case.check("the presented frame changed after Start "
               "(mean |delta| %.1f > 8)" % worst, responded)


def case_pad_after_load(case: Case) -> None:
    """Issue #117: a controller attached AFTER a save load. pad-late proves
    the same pad works when the run never loaded a payload, so this case
    isolates the load itself. It ends on the POLL side -- FUN_006285c0's own
    ten device-interface pointers and its per-frame polled mask -- because
    "the game reads nothing" has several causes and only that array
    distinguishes them."""
    case.prepare_profile(["boot.mode=continue"])
    case.seed_save("autosave.save")
    case.launch({
        "X2_FILES": "1",
        "X2_SCRIPTS": "1",
        "X2_VIRTUAL_PAD": "f2000",
    })
    case.wait_control(60)

    case.check("the save was loaded through boot Continue",
               case.wait_log("BOOT MODE:", 180))
    case.check("the destination map opened", case.wait_log(".pkgb", 300))
    case.check("controls unlocked after the replayed conversations",
               wait_controls_unlocked(case, 300))

    case.check("the synthetic pad attached after the load",
               case.wait_log("DINPUT-PAD: pad 0 connected", 180))
    case.check("hotswap re-entered the game's enumeration",
               case.wait_log("DINPUT8: HOTSWAP", 120))
    code, body = case.http("/assignment?player=1&pad=0")
    case.check("pad assigned to player 1 over the control channel",
               code == 200, body.decode(errors="replace").strip())
    time.sleep(2.0)

    # The poll side, printed in full whatever it says. A slot the game never
    # reads is a slot whose interface pointer is NULL (or whose GetDeviceState
    # fails) -- nothing else in FUN_006285c0 can gate it -- so this report
    # either names the gate or rules that shape out.
    report = case.get_text("/input?controller=0")
    (case.dir / "poll-side.txt").write_text(report)
    # Only the poll-side block: the report has other "slot N ..." lines (the
    # live-GUID table), and counting those would make ten slots read as more.
    poll, inside = [], False
    for line in report.splitlines():
        if "dinput8 poll side" in line:
            inside = True
        if inside:
            poll.append(line.rstrip())
        if "slot(s) hold a device interface" in line:
            inside = False
    print("\n".join(poll) if poll else
          "  (the poll-side probe printed NOTHING -- that is a probe defect)")
    case.check("the poll-side probe reported all ten slots",
               sum(1 for line in poll if line.strip().startswith("slot ")) == 10)
    holds = any("0 of 10 slot(s) hold a device interface" not in line
                and "slot(s) hold a device interface" in line
                for line in poll)
    case.check("the manager holds at least one device interface after the "
               "post-load admission", holds,
               next((line.strip() for line in poll
                     if "hold a device interface" in line), "no summary line"))

    # Issue #117's own falsifier is a heartbeat that GROWS by thousands, not
    # a single polled frame: a probe-driven read would satisfy the mask check
    # while the game's own loop stayed dead.
    baseline = pad_poll_rate(case)
    polled = wait_pad_polled(case, 60)
    case.check("the game's own loop keeps reading the pad (heartbeat grew "
               "past the probe baseline)", polled,
               "%d -> %d button read(s)" % (baseline, pad_poll_rate(case)))

    before = case.shot("before-start")
    code, body = case.http("/pad?button=start&hold=2.0")
    case.check("Start press delivered to the synthetic pad", code == 200,
               body.decode(errors="replace").strip())
    responded, worst = False, 0.0
    deadline = time.monotonic() + 20
    while time.monotonic() < deadline and not responded:
        current = case.shot("after-start")
        worst = png_mean_diff(before, current)
        responded = worst > 8.0
        time.sleep(1.0)
    case.check("the presented frame changed after Start "
               "(mean |delta| %.1f > 8)" % worst, responded)
    (case.dir / "final-input-report.txt").write_text(
        case.get_text("/input?controller=0"))


def case_pad_persisted(case: Case) -> None:
    """A controller matching the STORED controller0 id is adopted for
    Player 1 with no session assignment at all -- the path a real pad takes
    when it appears after start and settings already name it. With
    --boot-continue the run boots through the Continue path first, which is
    the exact flow the controller-after-start report describes."""
    stored_id = "x2-test-pad-0001"
    boot_continue = getattr(case, "boot_continue", False)
    case.prepare_profile([
        "boot.mode=%s" % ("continue" if boot_continue else "normal"),
        "input.assignment_version=2",
        "input.keyboard0.player=0",
        "input.keyboard1.player=unassigned",
        "input.keyboard2.player=unassigned",
        "input.keyboard3.player=unassigned",
        "input.controller0.id=%s" % stored_id,
        "input.controller0.player=0",
        "input.controller1.id=",
        "input.controller1.player=unassigned",
        "input.controller2.id=",
        "input.controller2.player=unassigned",
        "input.controller3.id=",
        "input.controller3.player=unassigned",
    ])
    env = {
        # After the save load on the Continue path: the deserialized input
        # manager is what must adopt the pad, which is the reported scenario.
        "X2_VIRTUAL_PAD": "f4000" if boot_continue else "f600",
        "X2_VIRTUAL_PAD_ID": stored_id,
        "X2_FILES": "1",
        "X2_SCRIPTS": "1",
    }
    if boot_continue:
        case.seed_save("autosave.save")
    else:
        env["X2_BOOT_MAP"] = TUTORIAL_MAP
    case.launch(env)
    case.wait_control(60)
    if boot_continue:
        case.check("Continue boot reached the saved map",
                   case.wait_log("BOOT PLAYER:", 240))
        case.check("destination map opened before driving the pad",
                   case.wait_log("/maps/act0/tutorial/tutorial1.pkgb", 300))
        time.sleep(10)

    case.check("the synthetic identity was announced",
               case.wait_log("X2_VIRTUAL_PAD_ID", 60))
    reached = reach_authored_conversation(case, 240)
    case.check("authored conversation reached (gameplay context)", reached)
    if not reached:
        return
    code, body = case.http("/key?name=Escape&hold=0.4")
    case.check("keyboard Escape cleared the authored scene", code == 200,
               body.decode(errors="replace").strip())
    case.check("controls unlocked after the skip",
               wait_controls_unlocked(case, 180))

    adopted, detail = False, ""
    deadline = time.monotonic() + 60
    while time.monotonic() < deadline and not adopted:
        if not case.alive():
            break
        report = case.get_text("/input?controller=0")
        for line in report.splitlines():
            if "resolved players" in line:
                detail = line.strip()
                adopted = stored_id in line and "P1=%s" % stored_id in line
        time.sleep(1.0)
    case.check("player 1 resolved to the stored id with no session "
               "assignment", adopted, detail)
    case.check("the synthetic pad attached before driving",
               case.wait_log("DINPUT-PAD: pad 0 connected", 120))
    case.check("the game is polling the pad again after the load",
               wait_pad_polled(case, 120))

    before = case.shot("before-start")
    code, body = case.http("/pad?button=start&hold=2.0")
    case.check("Start press delivered to the synthetic pad", code == 200,
               body.decode(errors="replace").strip())
    responded, worst = False, 0.0
    deadline = time.monotonic() + 20
    while time.monotonic() < deadline and not responded:
        current = case.shot("after-start")
        worst = png_mean_diff(before, current)
        responded = worst > 8.0
        time.sleep(1.0)
    case.check("the presented frame changed after Start "
               "(mean |delta| %.1f > 8)" % worst, responded)


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


def case_deadzone_render(case: Case) -> None:
    """Code-spawn a Scourge Critter and prove the Dead Zone sea is textured
    and changing. This is deliberately a render observation, not a general
    gameplay gate: the unit/selftests own the renderer contracts."""
    case.prepare_profile(["boot.mode=normal"])
    case.launch({
        "X2_BOOT_MAP": "act1/deadzone/deadzone1",
        "X2_FILES": "1",
        "X2_SCRIPTS": "1",
        "X2_SPAWN_CRITTER": "1",
    })
    case.wait_control(60)
    case.check("the retail spawn path returned a Scourge Critter entity",
               case.wait_log("factory result", 180)
               and "factory result 0x00000000" not in case.log_text())
    case.check("the Scourge Critter model was requested",
               case.wait_log("actors/60_critter.igb", 180))
    case.check("R8G8B8 is no longer refused",
               "CreateTexture in format 20" not in case.log_text())

    # The bounded boot camera leaves a blue sea wedge in the upper-right.
    # Blue-pixel selection below excludes the rocks and foliage crossing it.
    time.sleep(2.0)
    first = case.shot("water-a")
    if case.proc:
        os.kill(case.proc.pid, signal.SIGUSR1)
        case.check("a full visible-frame draw table completed",
                   case.wait_log("[FRAME TABLE] end of frame", 30))
    time.sleep(1.0)
    second = case.shot("water-b")
    spread, delta = png_water_stats(first, second)
    # This camera exposes only a small, oblique wedge of sea; it cannot be
    # compared numerically with the user's horizon-facing reference crop.
    # It can still distinguish a flat fill from a textured surface, while the
    # GPU pixel selftest owns the mip-chain contract.
    case.check("the visible blue sea is not a flat fill (luma stddev > 1)",
               spread > 1.0,
               "stddev %.2f" % spread)
    case.check("the sea crop changes between frames (mean |delta| > 0.1)",
               delta > 0.1, "mean |delta| %.2f" % delta)


def case_deadzone_water(case: Case) -> None:
    """Measure the Dead Zone sea before a spawned enemy can pull the camera
    inland. The Critter reproduction remains in deadzone-render."""
    case.prepare_profile(["boot.mode=normal"])
    case.launch({
        "X2_BOOT_MAP": "act1/deadzone/deadzone1",
        "X2_FILES": "1",
        "X2_SCRIPTS": "1",
    })
    case.wait_control(60)
    case.check("the Dead Zone map opened",
               case.wait_log("maps/act1/deadzone/deadzone1.igb", 180))
    case.check("the Dead Zone entry script launched",
               case.wait_log('SCRIPT: launch "act1/deadzone/deadzone1/deadzone1"',
                             180))
    time.sleep(3.0)
    first = case.shot("water-a")
    time.sleep(1.0)
    second = case.shot("water-b")
    spread, delta = png_water_stats(first, second)
    case.check("the visible blue sea is not a flat fill (luma stddev > 1)",
               spread > 1.0,
               "stddev %.2f" % spread)
    case.check("the sea crop changes between frames (mean |delta| > 0.1)",
               delta > 0.1, "mean |delta| %.2f" % delta)


def png_water_stats(a: Path, b: Path) -> tuple[float, float]:
    from PIL import Image  # the locked uv environment owns Pillow
    try:
        ia = Image.open(a).convert("RGB")
        ib = Image.open(b).convert("RGB")
    except Exception as exc:
        print("  [WARN] water crop comparison unavailable: %s" % exc)
        return 0.0, 0.0
    if ia.size != ib.size:
        ia = ia.resize(ib.size)
    # The first version hardcoded the equivalent 800x600 box. Issue #135
    # made a fresh profile honor its configured 1280x720 output, exposing that
    # the diagnostic had encoded one resolution rather than the sea region.
    width, height = ib.size
    box = (round(width * 5 / 8), 0, width, round(height * 5 / 12))
    ia = ia.crop(box)
    ib = ib.crop(box)
    pa, pb = list(ia.getdata()), list(ib.getdata())
    # Ignore rocks/foliage in the diagnostic crop. Requiring both frames to
    # classify the pixel as blue also keeps a moving silhouette edge from
    # masquerading as animated water.
    def blue(pixel: tuple[int, int, int]) -> bool:
        red, green, blue_channel = pixel
        return (blue_channel > red * 1.2
                and blue_channel > green * 1.05
                and blue_channel > 50)

    pairs = [(x, y) for x, y in zip(pa, pb, strict=True)
             if blue(x) and blue(y)]
    if len(pairs) < 1000:
        return 0.0, 0.0
    first_blue = [round(0.299 * x[0] + 0.587 * x[1] + 0.114 * x[2])
                  for x, _ in pairs]
    mean = sum(first_blue) / len(first_blue)
    spread = (sum((x - mean) ** 2 for x in first_blue)
              / len(first_blue)) ** 0.5
    delta = sum(abs((0.299 * x[0] + 0.587 * x[1] + 0.114 * x[2])
                    - (0.299 * y[0] + 0.587 * y[1] + 0.114 * y[2]))
                for x, y in pairs) / len(pairs)
    return spread, delta
def case_selector_dialog(case: Case) -> None:
    """Reach New Game's difficulty dialog and record every draw bound to the
    observed 128x32 runtime texture class. The fingerprint and geometry, not
    the dimensions alone, distinguish the three same-sized resources."""
    width, height = ((3840, 2160) if case.name.endswith("4k") else (800, 600))
    evidence = case.dir / "selector-128x32.jsonl"
    case.prepare_profile([
        "boot.mode=normal",
        "video.width=%d" % width,
        "video.height=%d" % height,
        "video.mode=windowed",
    ])
    case.launch({
        "X2_FILES": "1",
        "X2_SELECTOR_PROBE": str(evidence),
        "X2_SELECTOR_TEXTURE": "128x32",
    })
    case.wait_control(60)
    case.check("main-menu map lifecycle opened",
               case.wait_log("menus/main.pkgb", 300))
    time.sleep(5)
    case.shot("main-menu")
    code, body = case.http("/key?name=Return&hold=0.4")
    case.check("Return accepted on NEW GAME", code == 200,
               body.decode(errors="replace").strip())
    time.sleep(5)
    dialog = case.shot("difficulty-dialog")
    case.check("difficulty capture is a PNG",
               dialog.read_bytes().startswith(b"\x89PNG\r\n\x1a\n"))

    from selector_probe import parse_records, summarize
    try:
        summary = summarize(parse_records(evidence))
    except Exception as exc:
        case.check("128x32 selector evidence is parseable", False, str(exc))
        return
    fingerprints = {
        item["fingerprint"] for item in summary.candidates
        if item["fingerprint_available"]
    }
    case.check("128x32 draw requests reached the v5 probe",
               bool(summary.candidates), "%d request(s)" % len(summary.candidates))
    case.check("every recorded build request has a result",
               len(summary.results) == len(summary.candidates))
    case.check("runtime bytes identify at least one texture",
               bool(fingerprints), ", ".join(sorted(fingerprints)))

    expected_mode = "%dx%d" % (width, height)
    cold_log = case.log_text()
    case.check("cold profile creates the configured D3D device",
               "CreateDevice adapter=0 hardware-vertex %s" % expected_mode
               in cold_log)
    case.check("cold profile resolves through the retail settings reader",
               "DISPLAY RUNTIME: retail settings resolved configured mode %s"
               % expected_mode in cold_log)
    registry = (case.profile / "registry.txt").read_text(errors="replace")
    case.check("cold profile persists the configured retail Resolution",
               expected_mode.encode().hex() in registry)

    # The fresh-profile branch and Version=7 warm branch are different retail
    # code paths. Preserve the cold evidence, then boot the SAME profile again
    # so one passing branch cannot be presented as evidence for the other.
    case.shutdown()
    shutil.copy2(case.log_path, case.dir / "cold-run.log")
    warm_evidence = case.dir / "selector-128x32-warm.jsonl"
    case.log_path = case.dir / "warm-run.log"
    case.launch({
        "X2_FILES": "1",
        "X2_SELECTOR_PROBE": str(warm_evidence),
        "X2_SELECTOR_TEXTURE": "128x32",
    })
    case.wait_control(60)
    case.check("warm-profile main-menu lifecycle opened",
               case.wait_log("menus/main.pkgb", 300))
    warm_log = case.log_text()
    case.check("warm profile keeps the configured seed",
               "DISPLAY SEED: no change needed for video %s" % expected_mode
               in warm_log)
    case.check("warm profile creates the configured D3D device",
               "CreateDevice adapter=0 hardware-vertex %s" % expected_mode
               in warm_log)
    warm_shot = case.shot("warm-main-menu")
    try:
        from PIL import Image
        with Image.open(warm_shot) as image:
            warm_size = image.size
    except Exception:
        warm_size = (0, 0)
    case.check("warm capture has the configured dimensions",
               warm_size == (width, height), "%dx%d" % warm_size)


def png_mean_diff(a: Path, b: Path) -> float:
    from PIL import Image  # the locked uv environment owns Pillow
    try:
        ia, ib = Image.open(a).convert("L"), Image.open(b).convert("L")
    except Exception as exc:
        print("  [WARN] screenshot comparison unavailable: %s" % exc)
        return 0.0
    if ia.size != ib.size:
        ia = ia.resize(ib.size)
    pa, pb = list(ia.getdata()), list(ib.getdata())
    n = min(len(pa), len(pb))
    if not n:
        return 0.0
    step = max(1, n // 4096)
    count = len(range(0, n, step))
    return sum(abs(pa[i] - pb[i]) for i in range(0, n, step)) / count


CASES = {
    "cutscene-skip": case_cutscene_skip,
    "cutscene-skip-early": case_cutscene_skip_early,
    "boot-continue": case_boot_continue,
    "pad-late": case_pad_late,
    "pad-after-load": case_pad_after_load,
    "pad-persisted": case_pad_persisted,
    "manual-continue": case_manual_continue,
    "deadzone-render": case_deadzone_render,
    "deadzone-water": case_deadzone_water,
    "selector-dialog-800": case_selector_dialog,
    "selector-dialog-4k": case_selector_dialog,
}


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("case", nargs="?", choices=sorted(CASES))
    ap.add_argument("--port", type=int, default=DEFAULT_PORT)
    ap.add_argument("--binary", type=Path, default=BINARY,
                    help="which x2native build to run (A/B against an older "
                         "binary)")
    ap.add_argument("--boot-continue", action="store_true",
                    help="pad-persisted: boot through Continue first")
    ap.add_argument("--pacing", choices=("paced", "uncapped", "fast"),
                    default="fast",
                    help="fast = X2_UNPACED+unbounded scheduler; uncapped "
                         "= no guest frame cap only; paced = retail pacing")
    args = ap.parse_args()
    if not args.case:
        ap.print_help()
        return 2
    global PACING
    PACING = args.pacing
    Case.boot_continue = args.boot_continue
    if not args.binary.is_file():
        refuse("%s does not exist" % args.binary)
    SELECTED["path"] = args.binary
    case = Case(args.case, args.port)
    try:
        CASES[args.case](case)
    except BaseException:
        case.shutdown()
        raise
    return case.finish()


if __name__ == "__main__":
    sys.exit(main())
