#!/usr/bin/env python3
"""Drive a running x2native from outside it.

Start the game with the control channel open:

    scratch/build-native/x2native --no-window --unbounded --control &

then talk to it while it runs:

    tools/x2ctl.py status                 # frames, guest time, frame timing
    tools/x2ctl.py key Return             # press a key (--hold SECONDS)
    tools/x2ctl.py key Escape Escape Up   # several, in order
    tools/x2ctl.py pad a start             # synthetic pad buttons
    tools/x2ctl.py pad leftx=-1            # ... and axes
    tools/x2ctl.py shot out.png           # capture the current frame
    tools/x2ctl.py input                  # the GAME's bindings + live actions
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
import json
import sys
import time
import urllib.error
import urllib.request

DEFAULT_PORT = 8420


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
    print("  frame time  avg %.2f ms  min %.2f ms  max %.2f ms  over %d interval(s)"
          % (s["frame_ms_avg"], s["frame_ms_min"], s["frame_ms_max"],
             s["frame_intervals"]))
    c = s["control"]
    print("  control     %d request(s), %d key(s) pressed, %d refused, %d shot(s)"
          % (c["requests"], c["keys_pressed"], c["keys_refused"],
             c["screenshots"]))
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


def main():
    p = argparse.ArgumentParser(description=__doc__,
                                formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--port", type=int, default=DEFAULT_PORT)
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

    w = sub.add_parser("watch")
    w.add_argument("--for", dest="duration", type=float, default=30.0)
    w.add_argument("--every", type=float, default=1.0)
    w.set_defaults(fn=cmd_watch)

    args = p.parse_args()
    return args.fn(args)


if __name__ == "__main__":
    sys.exit(main())
