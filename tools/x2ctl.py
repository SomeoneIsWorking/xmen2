#!/usr/bin/env python3
"""Drive a running x2native from outside it.

The default launcher opens the control channel, records the exact input states
returned to the game, and publishes how to find the run:

    ./run.sh

then talk to it while it runs:

    tools/x2ctl.py probe                  # one live diagnostic bundle
    tools/x2ctl.py status                 # frames, guest time, frame timing
    tools/x2ctl.py key Return             # press a key (--hold SECONDS)
    tools/x2ctl.py key Escape Escape Up   # several, in order
    tools/x2ctl.py pad a start             # synthetic pad buttons
    tools/x2ctl.py pad leftx=-1            # ... and axes
    tools/x2ctl.py assignment 2 --pad 0    # session-only pad -> Player 2
    tools/x2ctl.py assignment 2 --clear    # remove that eligibility
    tools/x2ctl.py shot out.png           # capture the current frame
    tools/x2ctl.py input                  # the GAME's bindings + live actions
    tools/x2ctl.py save                   # bounded retail save/load evidence
    tools/x2ctl.py recording --events 20  # tail the automatic JSONL trace
    tools/x2ctl.py watch --for 30         # print /status once a second

This exists because the alternative is deciding every input before launch and
reading the log afterwards. A frame-scheduled input script fires its presses
whether or not the game reached the state they were written for, so a run that
drifted answers a menu late, spends every press in the menus, draws a plausible
picture and reports success. Two such runs were read as evidence before a file
gate caught them. Ask the game where it is instead of assuming.

Nothing here is a gate. It observes and it drives; what passes or fails is
decided by tests that do not need the game to be playing.
"""

import argparse
from collections import deque
import json
import os
from pathlib import Path
import sys
import time
import urllib.error
import urllib.request

DEFAULT_PORT = 8420
ROOT = Path(__file__).resolve().parents[1]
LIVE_SESSION_PATH = ROOT / "scratch" / "run" / "live.json"


def read_live_session(path=LIVE_SESSION_PATH):
    """Read the launcher-published run identity, or return None if absent.

    A stale record is retained because it still names the last input recording;
    callers that require a live process separately check `running` and the PID.
    """
    try:
        with open(path, encoding="utf-8") as file:
            session = json.load(file)
    except (OSError, ValueError):
        return None
    if not isinstance(session, dict) or session.get("version") != 1:
        return None
    return session


def pid_is_alive(pid):
    try:
        os.kill(int(pid), 0)
    except (OSError, TypeError, ValueError):
        return False
    return True


def resolve_port(explicit, session=None):
    if explicit is not None:
        return explicit
    if session is None:
        session = read_live_session()
    if session and session.get("running") and pid_is_alive(session.get("pid")):
        port = session.get("control_port")
        if isinstance(port, int) and 0 < port < 65536:
            return port
    return DEFAULT_PORT


def call(port, path, timeout=15.0):
    """One request. Returns (status, content_type, body-bytes).

    A refusal from the game is not an exception: the server answers 409 with a
    reason ("all injection slots held", "no frame to capture") and 504 when the
    guest never polled, and each of those is an ANSWER about the run that the
    caller needs to see, not a transport failure to be retried.
    """
    url = "http://127.0.0.1:%d%s" % (port, path)
    try:
        with urllib.request.urlopen(url, timeout=timeout) as r:
            return r.status, r.headers.get("Content-Type", ""), r.read()
    except urllib.error.HTTPError as e:
        return e.code, e.headers.get("Content-Type", ""), e.read()
    except urllib.error.URLError as e:
        raise SystemExit(  # noqa: B904 -- the reason IS the message below
            "x2ctl: nothing is listening on 127.0.0.1:%d (%s).\n"
            "  The run needs --control (or X2_CONTROL=%d); without it the game "
            "takes no live commands.\n"
            "  A run that is merely BUSY still answers -- this means no server, "
            "not a slow game." % (port, e.reason, port))


def cmd_status(args):
    code, _, body = call(args.port, "/status")
    if code != 200:
        raise SystemExit("x2ctl: /status returned %d:\n%s"
                         % (code, body.decode(errors="replace")))
    s = json.loads(body)
    print("frame %-8d  guest %8.2fs  %s"
          % (s["frames_presented"], s["guest_time_s"],
             "UNBOUNDED" if s["unbounded"] else "wall-clock paced"))
    if "renderer_backend" in s:
        print("  renderer    %s  %dx%d" % (s["renderer_backend"],
              s.get("presentation_width", 0), s.get("presentation_height", 0)))
    print("  frame time  avg %.2f ms  min %.2f ms  max %.2f ms  over %d interval(s)"
          % (s["frame_ms_avg"], s["frame_ms_min"], s["frame_ms_max"],
             s["frame_intervals"]))
    percentile_keys = ("frame_ms_p50", "frame_ms_p95", "frame_ms_p99")
    if all(key in s for key in percentile_keys):
        print("  percentiles p50 %.2f ms  p95 %.2f ms  p99 %.2f ms  over %d sample(s)"
              % (s["frame_ms_p50"], s["frame_ms_p95"], s["frame_ms_p99"],
                 s.get("frame_sample_count", 0)))
    c = s["control"]
    print("  control     %d request(s), %d key(s) pressed, %d refused, %d shot(s)"
          % (c["requests"], c["keys_pressed"], c["keys_refused"],
             c["screenshots"]))
    recording = s.get("input_recording", {})
    print("  process     pid %s" % s.get("pid", "unknown"))
    if recording.get("path"):
        print("  recording   %d changed state(s) in %s"
              % (recording.get("events", 0), recording["path"]))
    else:
        print("  recording   DISABLED for this run")
    return 0


def cmd_key(args):
    bad = 0
    for name in args.names:
        path = "/key?name=%s" % name
        if args.hold:
            path += "&hold=%g" % args.hold
        code, _, body = call(args.port, path)
        text = body.decode(errors="replace").strip()
        print(("  " if code == 200 else "  REFUSED(%d) " % code) + text)
        if code != 200:
            bad += 1
        elif args.gap:
            time.sleep(args.gap)
    # A refused press is a failure of the command, not a hiccup to swallow:
    # the caller pressed a key and it did not happen.
    return 1 if bad else 0


def cmd_pad(args):
    """Press buttons / move axes on the SYNTHETIC pad (X2_VIRTUAL_PAD).

    A real controller is driven by the hardware and never from here; this
    exists because SDL's virtual joystick reads zero on every button until
    something sets one, so a headless run could enumerate a pad and prove
    nothing about a press reaching the game.
    """
    bad = 0
    for name in args.names:
        if "=" in name:                       # axis form: leftx=-1
            what, _, val = name.partition("=")
            path = "/pad?axis=%s&value=%s" % (what, val)
        else:
            path = "/pad?button=%s" % name
        if args.hold:
            path += "&hold=%g" % args.hold
        code, _, body = call(args.port, path)
        text = body.decode(errors="replace").strip()
        print(("  " if code == 200 else "  REFUSED(%d) " % code) + text)
        if code != 200:
            bad += 1
        elif args.gap:
            time.sleep(args.gap)
    return 1 if bad else 0


def cmd_assignment(args):
    """Assign one exact live pad for this process, without persisting it."""
    path = "/assignment?player=%d&" % args.player
    path += "clear=1" if args.clear else "pad=%d" % args.pad
    code, _, body = call(args.port, path)
    text = body.decode(errors="replace").strip()
    print(("  " if code == 200 else "  REFUSED(%d) " % code) + text)
    return 0 if code == 200 else 1


def cmd_shot(args):
    code, ctype, body = call(args.port, "/screenshot", timeout=30.0)
    if code != 200 or "png" not in ctype:
        raise SystemExit("x2ctl: no screenshot (%d):\n%s"
                         % (code, body.decode(errors="replace")))
    with open(args.out, "wb") as f:
        f.write(body)
    print("%s  %d bytes" % (args.out, len(body)))
    return 0


def cmd_input(args):
    """The GAME's own input state -- its binding table and which actions read
    down right now.

    This is the other half of `pad`: `pad` proves the host set a button, this
    proves (or disproves) that the game turned it into an action. Hold the
    button while asking, or the press will have expired by the time the report
    is taken: `x2ctl.py pad a --hold 3 & sleep 1; x2ctl.py input`.
    """
    code, _, body = call(args.port, "/input?controller=%d" % args.controller,
                         timeout=30.0)
    text = body.decode(errors="replace")
    if code != 200:
        raise SystemExit("x2ctl: /input returned %d:\n%s" % (code, text))
    sys.stdout.write(text if text.endswith("\n") else text + "\n")
    return 0


def fetch_save_report(port):
    code, _, body = call(port, "/save", timeout=30.0)
    text = body.decode(errors="replace")
    if code != 200:
        raise SystemExit("x2ctl: /save returned %d:\n%s" % (code, text))
    return body, text


def cmd_save(args):
    _, text = fetch_save_report(args.port)
    sys.stdout.write(text if text.endswith("\n") else text + "\n")
    return 0


def cmd_watch(args):
    end = time.time() + args.duration
    last = None
    while time.time() < end:
        code, _, body = call(args.port, "/status")
        if code != 200:
            raise SystemExit("x2ctl: /status returned %d" % code)
        s = json.loads(body)
        f = s["frames_presented"]
        rate = "" if last is None else "  (+%d frames)" % (f - last)
        # A frame count that does not move is the point of watching, so it is
        # printed exactly like one that does, never skipped as "no change".
        print("frame %-8d  guest %8.2fs  avg %.2f ms%s"
              % (f, s["guest_time_s"], s["frame_ms_avg"], rate))
        last = f
        time.sleep(args.every)
    return 0


def recording_path(session):
    if not session or not session.get("input_recording"):
        raise SystemExit("x2ctl: the live-session record names no input recording")
    path = Path(session["input_recording"])
    if not path.is_absolute():
        path = ROOT / path
    path = path.resolve()
    try:
        path.relative_to(ROOT)
    except ValueError:
        raise SystemExit("x2ctl: refusing an input recording outside the repo: %s"
                         % path) from None
    return path


def print_recording_tail(session, count):
    path = recording_path(session)
    try:
        with open(path, encoding="utf-8") as file:
            lines = deque(file, maxlen=count)
    except OSError as error:
        raise SystemExit("x2ctl: cannot read %s: %s" % (path, error)) from None
    print("recent input (%s):" % path.relative_to(ROOT))
    for line in lines:
        print("  " + line.rstrip())
    return path


def cmd_recording(args):
    print_recording_tail(args.live_session, args.events)
    return 0


def cmd_probe(args):
    """Capture one inspectable bundle from the currently published run."""
    session = args.live_session
    if not session or not session.get("running") or not pid_is_alive(session.get("pid")):
        raise SystemExit(
            "x2ctl: scratch/run/live.json does not name a running product.\n"
            "  Start ./run.sh; it now publishes the run automatically.")

    print("live probe -- published pid %d, port %d"
          % (session["pid"], args.port))
    cmd_status(args)

    code, _, body = call(args.port,
                         "/input?controller=%d" % args.controller,
                         timeout=30.0)
    if code != 200:
        raise SystemExit("x2ctl: /input returned %d:\n%s"
                         % (code, body.decode(errors="replace")))
    input_path = ROOT / "scratch" / "logs" / "live-probe-input.txt"
    input_path.parent.mkdir(parents=True, exist_ok=True)
    input_path.write_bytes(body)
    text = body.decode(errors="replace")
    down = [line.strip() for line in text.splitlines()
            if line.rstrip().endswith("DOWN")]
    print("  guest input full report: %s" % input_path.relative_to(ROOT))
    print("  actions down: %s" % (", ".join(down) if down else "none"))

    save_body, save_text = fetch_save_report(args.port)
    save_path = ROOT / "scratch" / "logs" / "live-probe-save.txt"
    save_path.write_bytes(save_body)
    save_header = save_text.splitlines()[0] if save_text else "empty report"
    print("  save trace: %s (%s)"
          % (save_path.relative_to(ROOT), save_header))

    code, ctype, shot = call(args.port, "/screenshot", timeout=30.0)
    if code == 200 and "png" in ctype:
        shot_path = ROOT / args.shot
        shot_path.parent.mkdir(parents=True, exist_ok=True)
        shot_path.write_bytes(shot)
        print("  frame capture: %s (%d bytes)"
              % (shot_path.relative_to(ROOT), len(shot)))
    else:
        print("  frame capture unavailable: %s"
              % shot.decode(errors="replace").strip())

    print_recording_tail(session, args.events)
    return 0


def main():
    p = argparse.ArgumentParser(description=__doc__,
                                formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--port", type=int, default=None,
                   help="control port; defaults to the run published in "
                        "scratch/run/live.json, then 8420")
    sub = p.add_subparsers(dest="cmd", required=True)

    sub.add_parser("status").set_defaults(fn=cmd_status)

    k = sub.add_parser("key")
    k.add_argument("names", nargs="+")
    k.add_argument("--hold", type=float, default=0.0,
                   help="seconds to hold each key (default: the game's 0.3)")
    k.add_argument("--gap", type=float, default=0.4,
                   help="seconds between presses")
    k.set_defaults(fn=cmd_key)

    d = sub.add_parser("pad", help="press a synthetic pad button, or move an "
                                   "axis with NAME=VALUE (leftx=-1)")
    d.add_argument("names", nargs="+")
    d.add_argument("--hold", type=float, default=0.0)
    d.add_argument("--gap", type=float, default=0.4)
    d.set_defaults(fn=cmd_pad)

    assignment = sub.add_parser(
        "assignment", help="assign a session-only live pad to a player")
    assignment.add_argument("player", type=int, choices=range(1, 5))
    assignment_mode = assignment.add_mutually_exclusive_group(required=True)
    assignment_mode.add_argument("--pad", type=int,
                                 help="current live pad index")
    assignment_mode.add_argument("--clear", action="store_true")
    assignment.set_defaults(fn=cmd_assignment)

    s = sub.add_parser("shot")
    s.add_argument("out", nargs="?", default="scratch/screenshots/x2ctl.png")
    s.set_defaults(fn=cmd_shot)

    ip = sub.add_parser("input", help="the game's binding table and live "
                                      "action state")
    ip.add_argument("--controller", type=int, default=0,
                    help="which binding set to print (0..3 are the masters "
                         "the options UI edits; 4..7 are the working sets the "
                         "game evaluates; 12..15 are the menu sets)")
    ip.set_defaults(fn=cmd_input)

    sub.add_parser("save", help="bounded retail save/load transition trace "
                                  "(X2_SAVE_TRACE=0 disables it)").set_defaults(
                                      fn=cmd_save)

    w = sub.add_parser("watch")
    w.add_argument("--for", dest="duration", type=float, default=30.0)
    w.add_argument("--every", type=float, default=1.0)
    w.set_defaults(fn=cmd_watch)

    rec = sub.add_parser("recording", help="show the latest exact input-state "
                                            "changes from the published run")
    rec.add_argument("--events", type=int, default=20)
    rec.set_defaults(fn=cmd_recording)

    probe = sub.add_parser("probe", help="inspect the published live game: "
                                           "status, guest input, save trace, "
                                           "frame, and recorded inputs")
    probe.add_argument("--controller", type=int, default=0)
    probe.add_argument("--events", type=int, default=20)
    probe.add_argument("--shot", default="scratch/screenshots/live-probe.png")
    probe.set_defaults(fn=cmd_probe)

    args = p.parse_args()
    args.live_session = read_live_session()
    args.port = resolve_port(args.port, args.live_session)
    return args.fn(args)


if __name__ == "__main__":
    sys.exit(main())
