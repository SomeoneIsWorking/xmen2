#!/usr/bin/env python3
"""Reading a running browser's console, workers included.

One flattened auto-attach carries every target's output on a single socket,
so a worker that starts DURING the run is picked up as it appears. That
matters here because the product's own threads -- the heartbeat among them --
are workers: a page-only reader sees the loader and none of the game.

This is the one implementation. tools/web_console.py records with it and
tools/web_touch_play.py waits on it; a second copy would drift from whichever
of them was fixed first.
"""

from __future__ import annotations

import json

from cdp_client import Cdp, CdpError

# Log.entryAdded as well as Runtime.consoleAPICalled: the first carries
# browser-generated warnings and, importantly, anything written to stderr by a
# native library rather than through console.*.
CONSOLE_EVENTS = ("Runtime.consoleAPICalled", "Log.entryAdded")


def _argument_text(argument: dict) -> str:
    if "value" in argument:
        value = argument["value"]
        return value if isinstance(value, str) else json.dumps(value)
    return argument.get("description") or argument.get("unserializableValue") or ""


def console_line(event: dict) -> str | None:
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


def attach_all_targets(cdp: Cdp) -> dict[str, str]:
    """Auto-attach to the page and every worker; return sessionId -> target url."""
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
