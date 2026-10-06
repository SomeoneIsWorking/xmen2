#!/usr/bin/env python3
"""Run the browser product's Dead Zone gameplay test in Zen and time its frames.

WHY THIS EXISTS. The players who reported the web build's behaviour use Zen, a
Firefox derivative, and every browser tool beside this one drives Chrome over
CDP. Firefox speaks Marionette instead, and its worker console output reaches
nothing but the browser's own stdout, so the Zen measurement has to own the
browser process end to end: serve a release, launch a throwaway headless
profile, install the game once, click `#test-play`, and read the heartbeat
from stdout.

WHAT IT REPORTS. The product's own per-heartbeat frame-time percentiles (the
`frame ms p50 .. p95 .. p99` line, taken over only the intervals since the
previous heartbeat) for every window after the warm-up, then their summary:
the median window's p50 and p95, the worst window's p99, and the steady
present rate over the plateau (tools/web_presents.py owns that arithmetic).

THE NEGATIVE IS DESIGNED. A browser that printed nothing, a run whose
heartbeat never carried a frame, and a run whose windows all fell inside the
warm-up are three different failures; each is named and exits non-zero, never
an empty table.

It serves the release WITHOUT isolation headers, as GitHub Pages does: the
service worker supplies them, and a server that adds them would hide a broken
worker. The browser profile lives under scratch/zen/ and is reused, so the
game is installed into its private storage once, not on every run.
"""

from __future__ import annotations

import argparse
import re
import signal
import socket
import subprocess
import sys
import time
from pathlib import Path
from statistics import median

sys.path.insert(0, str(Path(__file__).resolve().parent))
from marionette_client import Marionette, MarionetteError
from web_presents import plateau, samples, steady

ROOT = Path(__file__).resolve().parent.parent
ZEN_APP = "app.zen_browser.zen"
HEARTBEAT_SECONDS = 5.0

# `[HB]           frame ms p50 119.42 p95 140.10 p99 180.00 over 42 interval(s)`
_WINDOW = re.compile(
    r"frame ms p50 ([0-9.]+) p95 ([0-9.]+) p99 ([0-9.]+) over (\d+) interval")
_ELAPSED = re.compile(r"\[HB\]\s+([0-9]+\.[0-9])s\s+crossings")

PREFS = {
    "dom.webgpu.enabled": True,
    "devtools.console.stdout.content": True,
    "browser.shell.checkDefaultBrowser": False,
    "browser.aboutwelcome.enabled": False,
    "datareporting.policy.dataSubmissionEnabled": False,
    "toolkit.telemetry.reportingpolicy.firstRun": False,
    "app.update.enabled": False,
}


def windows(text: str) -> list[tuple[float, float, float, float, int]]:
    """Every (elapsed, p50, p95, p99, intervals) window, in heartbeat order."""
    found = []
    elapsed: float | None = None
    for line in text.splitlines():
        match = _ELAPSED.search(line)
        if match:
            elapsed = float(match.group(1))
            continue
        match = _WINDOW.search(line)
        if match and elapsed is not None:
            found.append((elapsed, float(match.group(1)), float(match.group(2)),
                          float(match.group(3)), int(match.group(4))))
    return found


def summarize(text: str, warmup: float, window: int) -> int:
    """Print the run's frame-time windows and summary; non-zero when it measured nothing."""
    if not text.strip():
        print("zen_play: the browser printed nothing, so this measured "
              "nothing -- check devtools.console.stdout.content", file=sys.stderr)
        return 1
    every = windows(text)
    if not every:
        print("zen_play: no heartbeat carried a frame-time window -- the route "
              "never started, or this build predates the p50/p95/p99 line",
              file=sys.stderr)
        return 1
    measured = [w for w in every if w[0] >= warmup and w[4] > 0]
    print(f"{'elapsed':>8}  {'frames':>6}  {'p50 ms':>8}  {'p95 ms':>8}  {'p99 ms':>8}")
    for elapsed, p50, p95, p99, count in every:
        mark = "" if (elapsed, p50, p95, p99, count) in measured else "  (warm-up)"
        print(f"{elapsed:8.1f}  {count:6d}  {p50:8.2f}  {p95:8.2f}  {p99:8.2f}{mark}")
    if not measured:
        print(f"zen_play: {len(every)} window(s), none with frames after the "
              f"{warmup:.0f}s warm-up -- record for longer", file=sys.stderr)
        return 1
    print(f"frame time over {len(measured)} window(s), "
          f"{sum(w[4] for w in measured)} frames: "
          f"median p50 {median(w[1] for w in measured):.2f} ms, "
          f"median p95 {median(w[2] for w in measured):.2f} ms, "
          f"worst p99 {max(w[3] for w in measured):.2f} ms")
    found = [s for s in samples(text) if s[0] >= warmup]
    band = plateau([delta / HEARTBEAT_SECONDS for _, _, delta in found], window)
    if band is None:
        print(f"zen_play: fewer than {window} heartbeats after the warm-up, so "
              "there is no plateau rate", file=sys.stderr)
        return 1
    print(steady(found, band, HEARTBEAT_SECONDS))
    return 0


def write_profile(profile: Path, marionette_port: int) -> None:
    profile.mkdir(parents=True, exist_ok=True)
    prefs = dict(PREFS, **{"marionette.port": marionette_port})
    lines = []
    for name, value in prefs.items():
        literal = ("true" if value else "false") if isinstance(value, bool) else str(value)
        lines.append(f'user_pref("{name}", {literal});')
    (profile / "user.js").write_text("\n".join(lines) + "\n")


def wait_for_port(port: int, seconds: float) -> None:
    deadline = time.monotonic() + seconds
    while time.monotonic() < deadline:
        try:
            socket.create_connection(("127.0.0.1", port), timeout=1).close()
            return
        except OSError:
            time.sleep(0.5)
    raise SystemExit(f"zen_play: nothing listened on 127.0.0.1:{port} within {seconds:.0f}s")


def page_state(browser: Marionette) -> dict:
    return browser.script(
        """const done = arguments[arguments.length - 1];
        const q = s => document.querySelector(s);
        done({status: q('#status') ? q('#status').textContent : null,
              failed: q('#status') ? q('#status').dataset.failed : null,
              archive: q('#archive') ? !q('#archive').disabled : false,
              testPlay: q('#test-play') ? !q('#test-play').disabled : false});""",
        10000)


def wait_for_page(browser: Marionette, seconds: float) -> dict:
    deadline = time.monotonic() + seconds
    state: dict = {}
    while time.monotonic() < deadline:
        state = page_state(browser)
        if state["failed"] == "true":
            raise SystemExit(f"zen_play: the page failed: {state['status']}")
        if state["archive"]:
            return state
        time.sleep(1.0)
    raise SystemExit(f"zen_play: the setup page never opened; it last said {state.get('status')!r}")


def install(browser: Marionette, url: str, archive: Path, seconds: float) -> None:
    element = browser.command("WebDriver:FindElement", {"using": "css selector", "value": "#archive"})
    reference = next(iter(element["value"].values()))
    browser.command("WebDriver:ElementSendKeys", {"id": reference, "text": str(archive)})
    # The import goes straight on into the retail boot rather than exiting,
    # so completion is the marker the page itself checks before enabling play.
    deadline = time.monotonic() + seconds
    while time.monotonic() < deadline:
        state = page_state(browser)
        if browser.script(
                """const done = arguments[arguments.length - 1];
                navigator.storage.getDirectory()
                  .then(root => root.getFileHandle('install.ready'))
                  .then(() => done(true), () => done(false));""", 10000):
            browser.navigate(url)
            return
        if state["failed"] == "true":
            raise SystemExit(f"zen_play: the install failed: {state['status']}")
        time.sleep(5.0)
    raise SystemExit("zen_play: the install did not finish in time")


def run(args: argparse.Namespace) -> int:
    release = args.release.resolve()
    if not (release / "x2native.wasm").is_file():
        raise SystemExit(f"zen_play: {release} has no x2native.wasm; build it with tools/build_web.py")
    profile = args.profile.resolve()
    write_profile(profile, args.marionette_port)
    log_path = profile.parent / "browser.log"
    query = "".join(f"&arg={a}" for a in args.arg)
    url = f"http://127.0.0.1:{args.http_port}/?zen{query}"

    server = subprocess.Popen(
        [sys.executable, "-m", "http.server", str(args.http_port), "--bind", "127.0.0.1",
         "--directory", str(release)],
        stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    grants = [f"--filesystem={profile}"]
    if args.archive:
        grants.append(f"--filesystem={args.archive.resolve().parent}:ro")
    with log_path.open("w") as log:
        browser_process = subprocess.Popen(
            ["flatpak", "run", *grants, "--env=MOZ_HEADLESS=1",
             f"--env=MOZ_HEADLESS_WIDTH={args.width}", f"--env=MOZ_HEADLESS_HEIGHT={args.height}",
             ZEN_APP, "--profile", str(profile), "--marionette", "--no-remote"],
            stdout=log, stderr=subprocess.STDOUT)
    print(f"zen_play: server pid {server.pid}, browser pid {browser_process.pid}, log {log_path}")
    browser: Marionette | None = None
    try:
        wait_for_port(args.http_port, 30)
        wait_for_port(args.marionette_port, 90)
        browser = Marionette(args.marionette_port)
        browser.navigate(url)
        state = wait_for_page(browser, 120)
        if not state["testPlay"]:
            if not args.archive:
                raise SystemExit("zen_play: this profile has no installation; pass --archive once")
            install(browser, url, args.archive.resolve(), args.install_seconds)
            state = wait_for_page(browser, 120)
            if not state["testPlay"]:
                raise SystemExit(f"zen_play: the install finished but play stayed disabled: {state['status']}")
        start = log_path.stat().st_size
        browser.command("WebDriver:ElementClick", {"id": next(iter(browser.command(
            "WebDriver:FindElement", {"using": "css selector", "value": "#test-play"})["value"].values()))})
        time.sleep(args.seconds)
        if args.screenshot:
            import base64
            args.screenshot.write_bytes(base64.b64decode(browser.screenshot()))
        with log_path.open("rb") as handle:
            handle.seek(start)
            text = handle.read().decode(errors="replace")
    finally:
        if browser is not None:
            try:
                browser.command("Marionette:Quit", {"flags": ["eForceQuit"]})
            except (MarionetteError, OSError):
                pass
            browser.close()
        try:
            browser_process.wait(timeout=30)
        except subprocess.TimeoutExpired:
            browser_process.send_signal(signal.SIGTERM)
            browser_process.wait(timeout=30)
        server.terminate()
        server.wait(timeout=10)
    (profile.parent / "run.log").write_text(text)
    return summarize(text, args.warmup, args.plateau)


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--release", type=Path, default=ROOT / "build/release/web",
                        help="the packaged web release to serve")
    parser.add_argument("--profile", type=Path, default=ROOT / "scratch/zen/profile",
                        help="the throwaway Zen profile, reused so the install survives")
    parser.add_argument("--archive", type=Path, default=None,
                        help="the game ZIP, needed only when the profile has no installation")
    parser.add_argument("--seconds", type=float, default=180.0, help="how long to record gameplay")
    parser.add_argument("--warmup", type=float, default=60.0,
                        help="heartbeat seconds before a window counts (boot and map load)")
    parser.add_argument("--plateau", type=int, default=6,
                        help="consecutive heartbeats the rate plateau must hold for")
    parser.add_argument("--arg", action="append", default=[],
                        help="an argument for the runtime, repeated as ?arg=")
    parser.add_argument("--http-port", type=int, default=8617)
    parser.add_argument("--marionette-port", type=int, default=2837)
    parser.add_argument("--width", type=int, default=1280)
    parser.add_argument("--height", type=int, default=720)
    parser.add_argument("--install-seconds", type=float, default=1800.0)
    parser.add_argument("--screenshot", type=Path, default=None)
    parser.add_argument("--log", type=Path, default=None,
                        help="summarize this recorded run.log instead of running")
    args = parser.parse_args(argv)
    if args.log:
        return summarize(args.log.read_text(errors="replace"), args.warmup, args.plateau)
    return run(args)


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
