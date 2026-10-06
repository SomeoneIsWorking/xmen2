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
# native library rather than through console.*. Runtime.exceptionThrown too:
# an uncaught trap on a worker otherwise reaches the console only as
# emscripten's one-line "worker sent an error!", which names no function.
CONSOLE_EVENTS = ("Runtime.consoleAPICalled", "Log.entryAdded", "Runtime.exceptionThrown")


def _argument_text(argument: dict) -> str:
    if "value" in argument:
        value = argument["value"]
        return value if isinstance(value, str) else json.dumps(value)
    return argument.get("description") or argument.get("unserializableValue") or ""


def _frames(stack: dict | None) -> str:
    """` @ fn <- fn ...` for a CDP stackTrace, innermost first, or "" if none.

    Only error-level entries carry one, so an ordinary line is unchanged. A
    wasm frame is `wasm-function[N]`; tools/web_profile.py's symbol map turns
    N into a name after the fact."""
    frames = (stack or {}).get("callFrames") or []
    if not frames:
        return ""
    return " @ " + " <- ".join(frame.get("functionName") or "(anonymous)" for frame in frames)


def console_line(event: dict) -> str | None:
    """One printable line, or None if this event carries no console text."""
    method = event.get("method")
    params = event.get("params") or {}
    if method == "Runtime.consoleAPICalled":
        text = " ".join(_argument_text(a) for a in params.get("args") or [])
        level = params.get("type", "log")
    elif method == "Log.entryAdded":
        entry = params.get("entry") or {}
        text = entry.get("text", "") + _frames(entry.get("stackTrace"))
        level = entry.get("level", "log")
    elif method == "Runtime.exceptionThrown":
        details = params.get("exceptionDetails") or {}
        thrown = details.get("exception") or {}
        # The description carries the stack, one frame per line; a log line
        # keeps it whole with the frames separated rather than cut off.
        text = " | ".join((thrown.get("description") or details.get("text", "")).splitlines())
        text += _frames(details.get("stackTrace"))
        level = "thrown"
    else:
        return None
    if not text:
        return None
    return f"{level:<7} {text}"


def attached_sessions(cdp: Cdp) -> list[tuple[str, dict]]:
    """Every (sessionId, targetInfo) for the pages and their dedicated workers.

    Dedicated workers are NOT listed by `Target.getTargets`: they are children
    of their page. The only way to reach one is to attach to the page and turn
    auto-attach on THAT session, which replays an `attachedToTarget` event for
    each worker that already exists and sends one for each worker made later.
    A browser-level auto-attach reaches the page and never its workers --
    measured: a worker's trap then arrived only as the page's one-line
    "worker sent an error!", with no stack, because the worker's own console
    was never subscribed to.
    """
    sessions: list[tuple[str, dict]] = []
    for page in cdp.call("Target.getTargets")["targetInfos"]:
        if page["type"] != "page":
            continue
        page_session = cdp.call(
            "Target.attachToTarget", {"targetId": page["targetId"], "flatten": True}
        )["sessionId"]
        sessions.append((page_session, page))
        cdp.call(
            "Target.setAutoAttach",
            {"autoAttach": True, "waitForDebuggerOnStart": False, "flatten": True},
            session=page_session,
        )
        cdp.drain(1.5)
    for event in cdp.events:
        if event.get("method") != "Target.attachedToTarget":
            continue
        params = event["params"]
        sessions.append((params["sessionId"], params["targetInfo"]))
    return sessions


def _subscribe(cdp: Cdp, session: str) -> None:
    for domain in ("Runtime", "Log"):
        try:
            cdp.call(f"{domain}.enable", session=session, timeout=10.0)
        except CdpError:
            # A target can die between attach and enable; that is not a
            # reason to abandon the other sessions, and it is reported in
            # the summary by that session simply producing no lines.
            pass


def attach_all_targets(cdp: Cdp) -> dict[str, str]:
    """Subscribe to the page's and every worker's console; sessionId -> url."""
    sessions: dict[str, str] = {}
    for session, info in attached_sessions(cdp):
        sessions[session] = info.get("url") or info.get("type", "?")
    for session in sessions:
        _subscribe(cdp, session)
    return sessions


def adopt_late_target(cdp: Cdp, event: dict, sessions: dict[str, str]) -> bool:
    """Subscribe to a worker that appeared after attach_all_targets; True if
    `event` was such an attach. The port's pthread pool is created when the
    module loads, which is when the player presses play -- after a recorder
    started beforehand has attached -- so without this every guest thread's
    own console, and every trap's stack, is missed."""
    if event.get("method") != "Target.attachedToTarget":
        return False
    params = event["params"]
    session = params["sessionId"]
    if session not in sessions:
        info = params["targetInfo"]
        sessions[session] = info.get("url") or info.get("type", "?")
        _subscribe(cdp, session)
    return True
