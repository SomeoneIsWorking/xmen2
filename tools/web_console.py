#!/usr/bin/env python3
"""Record every console line the browser port writes, from every thread.

WHY THIS EXISTS. The port's evidence -- the heartbeat's import timings, the
wall-time split, the self-test verdicts -- is written with the logger, which in
the browser ends up in the console. WebLua's `console` command reads a small
ring that a single 96-entry boundary-ring dump overruns, so the numbers that
were asked for are gone before they can be read. Measured: a Dead Zone route
left 50 lines in that ring, none of them the heartbeat.

It also reaches the WORKERS. The guest CPU, the JIT and the renderer all run on
pthread workers, and `weblua-ctl console` sees the page target only. A line
written by the worker that is doing the work is exactly the line worth having.

WHAT IT IS NOT. Not a browser launcher and not a driver: WebLua owns the
session, this attaches to the Chrome it already started. Point `--profile` at
that session's profile directory.

THE NEGATIVE IS DESIGNED. If nothing arrives, this says how many targets it saw
and how many sessions it attached, because "the run printed nothing" and "this
tool was never attached to anything" are different answers and an empty file
cannot tell them apart.
"""

from __future__ import annotations

import argparse
import re
import sys
import time

from cdp_client import Cdp, CdpError, browser_endpoint, devtools_port
from cdp_console import CONSOLE_EVENTS, attach_all_targets, console_line

def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--profile",
        required=True,
        help="the Chrome profile directory WebLua was given (holds DevToolsActivePort)",
    )
    parser.add_argument("--seconds", type=float, default=120.0, help="how long to record")
    parser.add_argument("--out", help="write the lines here as well as to stdout")
    parser.add_argument(
        "--until",
        help="stop early once a line matches this regular expression",
    )
    parser.add_argument(
        "--grep",
        help="print only lines matching this regular expression (all are still written to --out)",
    )
    args = parser.parse_args()

    stop = re.compile(args.until) if args.until else None
    keep = re.compile(args.grep) if args.grep else None

    port = devtools_port(args.profile)
    cdp = Cdp(browser_endpoint(port), timeout=5.0)
    try:
        sessions = attach_all_targets(cdp)
        if not sessions:
            print(
                f"web_console: attached to 0 targets on port {port}. "
                "Nothing can be recorded; is the page open in this profile?",
                file=sys.stderr,
            )
            return 2
        print(
            f"web_console: {len(sessions)} target(s): "
            + ", ".join(sorted({u.split('?')[0] or '(worker)' for u in sessions.values()})),
            file=sys.stderr,
        )
        handle = open(args.out, "w", encoding="utf-8") if args.out else None
        deadline = time.monotonic() + args.seconds
        count = 0
        try:
            closed = ""
            while time.monotonic() < deadline:
                cdp.events.clear()
                try:
                    cdp.drain(min(2.0, deadline - time.monotonic()))
                except CdpError as failure:
                    # A page that navigates or a worker that exits closes the
                    # socket. Recording ENDS there and says so: a traceback
                    # would throw away the lines already collected, which are
                    # usually the ones worth having.
                    closed = f"; recording ended early: {failure}"
                    deadline = 0.0
                for event in cdp.events:
                    if event.get("method") not in CONSOLE_EVENTS:
                        continue
                    line = console_line(event)
                    if line is None:
                        continue
                    count += 1
                    if handle:
                        handle.write(line + "\n")
                    if keep is None or keep.search(line):
                        print(line, flush=True)
                    if stop and stop.search(line):
                        deadline = 0.0
                        break
                if handle:
                    handle.flush()
        finally:
            if handle:
                handle.close()
        print(
            f"web_console: {count} console line(s) from {len(sessions)} target(s)"
            + (f"; full log in {args.out}" if args.out else "")
            + closed,
            file=sys.stderr,
        )
        return 0
    finally:
        cdp.close()


if __name__ == "__main__":
    raise SystemExit(main())
