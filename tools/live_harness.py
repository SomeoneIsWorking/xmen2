"""The live-case harness: one bounded x2native run on an isolated profile and
its own control port, its checks, screenshots and teardown."""

from __future__ import annotations

import json
import os
import shutil
import signal
import subprocess
import time
import urllib.error
import urllib.request
from dataclasses import dataclass, field
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
BINARY = ROOT / "build" / "native" / "x2native"
CASES_DIR = ROOT / "scratch" / "run" / "cases"
DEFAULT_PORT = 8461
TUTORIAL_MAP = "act0/tutorial/tutorial1"


def refuse(message: str) -> None:
    raise SystemExit("live_case: %s" % message)


@dataclass
class RunOptions:
    """What the command line chose for every case in a run."""
    binary: Path = BINARY
    # fast = X2_UNPACED + unbounded scheduler; uncapped = no guest frame cap.
    pacing: str = "fast"
    extra_settings: list[str] = field(default_factory=list)
    boot_continue: bool = False


class Case:
    def __init__(self, name: str, port: int,
                 options: RunOptions | None = None) -> None:
        self.name = name
        self.port = port
        self.options = options or RunOptions()
        self.dir = CASES_DIR / name / str(port)
        self.profile = self.dir / "profile"
        self.log_path = self.dir / "run.log"
        self.shot_dir = self.dir / "shots"
        self.proc: subprocess.Popen | None = None
        self.log_file = None
        self.checks: list[tuple[str, bool]] = []
        self.runtime_settings: list[str] = []

    # -- lifecycle -----------------------------------------------------------

    def prepare_profile(self, conf_lines: list[str]) -> None:
        if CASES_DIR.exists():
            shutil.rmtree(self.dir, ignore_errors=True)
        # Concurrent cases on one host must not find each other's games, and
        # each publishes its live.json beside its own artifacts.
        self.runtime_settings = ["lan.presence=0",
                                 "live.directory=%s" % self.dir]
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

    def launch(self, env_extra: dict[str, str], *, visible: bool = False) -> None:
        binary = self.options.binary
        if not binary.is_file():
            refuse("%s does not exist; build x2native first" % binary)
        env = dict(os.environ)
        env["X2_SAVE_DIR"] = str(self.profile)
        env["X2_LOG_DIR"] = str(self.profile / "logs")
        env["SDL_AUDIODRIVER"] = "dummy"
        cmd = [str(binary), "--d3d8", "--control=%d" % self.port]
        # --set is the binary's highest-precedence cvar source, above
        # x2native-runtime.conf and the X2_* environment. It is NOT the same
        # file as the profile's x2native.conf, which is the player settings
        # store and ignores a runtime cvar written into it.
        for setting in self.runtime_settings + self.options.extra_settings:
            cmd.append("--set")
            cmd.append(setting)
        if not visible:
            cmd.insert(1, "--no-window")
        if self.options.pacing in ("uncapped", "fast"):
            env["X2_UNPACED"] = "1"
        if self.options.pacing == "fast":
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
        code, body = self.http("/status")
        if code != 200:
            return -1
        return int(json.loads(body).get("frames_presented", -1))

    def alive(self) -> bool:
        return self.proc is not None and self.proc.poll() is None

    def signal(self, sig: int) -> None:
        """Send `sig` to the run, or say the run is gone.

        `if case.proc:` is not the question -- a Popen whose child has exited
        is still truthy, so the bare os.kill raised ProcessLookupError out of
        the case and the traceback replaced the report. A run that died is a
        result, and a case that cannot say so leaves a crash where a FAIL and
        a log path belong."""
        if not self.alive():
            refuse("the run exited before it could be signalled (rc=%s); "
                   "log: %s"
                   % (self.proc.returncode if self.proc else "never started",
                      self.log_path))
        os.kill(self.proc.pid, sig)

    def wait_control(self, timeout: float) -> None:
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            if not self.alive():
                refuse("the run exited early (rc=%s); log: %s"
                       % (self.proc.returncode, self.log_path))
            try:
                code, body = self.http("/status", timeout=2.0)
                if code == 200:
                    pid = json.loads(body).get("pid")
                    if pid != self.proc.pid:
                        refuse("port %d is answered by pid %s, not this "
                               "run's pid %d" % (self.port, pid, self.proc.pid))
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
