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
import json
import re
import sys
import time

from cdp_client import Cdp, CdpError, browser_endpoint, devtools_port

# The console methods a logger can reach. `Log.entryAdded` carries the rest --
# browser-generated warnings and, importantly, anything written to stderr by a
# native library rather than through console.*.
_CONSOLE_EVENTS = ("Runtime.consoleAPICalled", "Log.entryAdded")


def _argument_text(argument: dict) -> str:
    if "value" in argument:
        value = argument["value"]
        return value if isinstance(value, str) else json.dumps(value)
    return argument.get("description") or argument.get("unserializableValue") or ""


def _line(event: dict) -> str | None:
    """One printable line, or None if this event carries no console text."""
    method = event.get("method")
    params = event.get("params") or {}
    if method == "Runtime.consoleAPICalled":
        text = " ".join(_argument_text(a) for a in params.get("args") or [])
        level = params.get("type", "log")
    elif method == "Log.entryAdded":
        entry = params.get("entry") or {}
        text = entry.get("text", "")
        level = entry.get("level", "log")
    else:
        return None
    if not text:
        return None
    return f"{level:<7} {text}"


def _attach(cdp: Cdp) -> dict[str, str]:
    """Auto-attach to the page and every worker; return sessionId -> target url.

    Flattened sessions mean one socket carries every target's events, so a
    worker that starts DURING the run is picked up as it appears rather than
    only if it existed when this tool started.
    """
    sessions: dict[str, str] = {}
    cdp.call("Target.setDiscoverTargets", {"discover": True})
    cdp.call(
        "Target.setAutoAttach",
        {"autoAttach": True, "waitForDebuggerOnStart": False, "flatten": True},
    )
    # The attach events arrive as ordinary events on the same socket.
    cdp.drain(2.0)
    for event in cdp.events:
        if event.get("method") == "Target.attachedToTarget":
            info = event["params"]["targetInfo"]
            sessions[event["params"]["sessionId"]] = info.get("url", info.get("type", "?"))
    for session in sessions:
        for domain in ("Runtime", "Log"):
            try:
                cdp.call(f"{domain}.enable", session=session, timeout=10.0)
            except CdpError:
                # A target can die between attach and enable; that is not a
                # reason to abandon the other sessions, and it is reported in
                # the summary by that session simply producing no lines.
                pass
    return sessions


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
        sessions = _attach(cdp)
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
            while time.monotonic() < deadline:
                cdp.events.clear()
                cdp.drain(min(2.0, deadline - time.monotonic()))
                for event in cdp.events:
                    if event.get("method") not in _CONSOLE_EVENTS:
                        continue
                    line = _line(event)
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
            + (f"; full log in {args.out}" if args.out else ""),
            file=sys.stderr,
        )
        return 0
    finally:
        cdp.close()


if __name__ == "__main__":
    raise SystemExit(main())
