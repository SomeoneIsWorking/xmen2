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
        cmd = [str(binary), "--no-window", "--d3d8",
               "--control=%d" % self.port]
        if PACING in ("uncapped", "fast"):
            env["X2_UNPACED"] = "1"
        if PACING == "fast":
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
        if "controls-locked yes" in report and "visible no" in report:
            return report
        time.sleep(0.5)
    return ""


def reach_authored_conversation(case: Case, timeout: float) -> bool:
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        if not case.alive():
            return False
        if "authored conversation: yes; visible yes" \
                in case.get_text("/input?controller=0"):
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
        if "controls-locked no" in report and "visible no" in report:
            return True
        time.sleep(1.0)
    return False


def case_cutscene_skip(case: Case) -> None:
    """Escape skips the tutorial scene WITHOUT the issue #83 softlock: the
    adjacent second conversation starts WITH a line, cleanup scripts launch,
    controls unlock."""
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
    idle_line = [line.strip() for line in report.splitlines()
                 if "authored skip" in line]
    case.check("authored skip latch retired and reported advances",
               any("idle" in line for line in idle_line)
               and any("advance(s)" in line for line in idle_line),
               "; ".join(idle_line))
    case.shot("after-skip")


def case_cutscene_skip_early(case: Case) -> None:
    """Escape during the CAMERA-ONLY opening stretch skips the whole authored
    sequence, not just whichever record happens to be on screen. The press
    lands while the control lock holds and nothing is visible, so it goes
    through the disabled/invisible update exits rather than the action gate --
    a different code path from case_cutscene_skip's press on a visible line,
    and the one a player actually hits first."""
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
                     if "authored conversation:" in line),
                    "controls never locked with nothing visible"))
    if not report:
        return
    visible_before = sum('conversation start "' in line
                         for line in case.log_text().splitlines())
    case.shot("before-escape")

    code, body = case.http("/key?name=Escape&hold=0.4")
    case.check("Escape accepted by the guest poll", code == 200,
               body.decode(errors="replace").strip())

    # The latch must ARM here. Without the camera-only arming path the press
    # is simply dropped, the sequence plays out in full, and every check
    # below still eventually passes on the authored timeline -- so the
    # request counter, not the outcome, is what discriminates.
    armed, status = False, ""
    deadline = time.monotonic() + 60
    while time.monotonic() < deadline and not armed:
        if not case.alive():
            break
        for line in case.get_text("/input?controller=0").splitlines():
            if "authored skip" in line:
                status = line.strip()
                armed = " 0 request(s)" not in status
        if not armed:
            time.sleep(1.0)
    case.check("the camera-only Escape armed the authored-skip latch",
               armed, status)

    case.check("conversation-end script conv_0020b_end launched",
               case.wait_log("conv_0020b_end", 240))
    unlocked = wait_controls_unlocked(case, 180)
    case.check("controls unlocked after the skip", unlocked)

    report = case.get_text("/input?controller=0")
    (case.dir / "final-input-report.txt").write_text(report)
    status = next((line.strip() for line in report.splitlines()
                   if "authored skip" in line), "")
    case.check("the latch retired after advancing the sequence's records",
               "idle" in status and " 0 retail response advance(s)"
               not in status, status)
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

    # The loaded save replays the level's authored intro conversations. With
    # no resolved hero the second one collides on the seen-line bitmap and
    # never shows a line (issue #83), which is the reported Continue
    # softlock; with the player resolved it plays and hands controls back.
    case.check("the adjacent conversation started with a line",
               case.wait_log('1_introlevel_0020b" -> STARTED', 240))
    line_ok = any('0020b" -> STARTED' in line and "0x13" in line
                  for line in case.log_text().splitlines())
    case.check("0020b is visible with a selected line (no seen-bit "
               "collision)", line_ok)
    # The replayed conversation waits for the player like retail's own intro;
    # advance it the way a player does (the authored-skip Escape) and require
    # the normal handoff instead of a softlock. The end script's FILE open is
    # a level-load preload, so the LAUNCH line is the completion evidence.
    if not wait_controls_unlocked(case, 20):
        code, body = case.http("/key?name=Escape&hold=0.4")
        case.check("Escape accepted on the resumed conversation", code == 200,
                   body.decode(errors="replace").strip())
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
    "pad-persisted": case_pad_persisted,
    "manual-continue": case_manual_continue,
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
