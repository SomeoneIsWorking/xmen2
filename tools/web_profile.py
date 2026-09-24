#!/usr/bin/env python3
"""Record a CPU profile of the browser port's worker, with a denominator.

WHY. In the browser, everything that matters runs on a pthread worker: the
guest's x86 blocks, the JIT that translates them, the host import stubs and the
renderer submission. The port's own wall-time split only attributes time inside
*nested* dispatch spans -- the outermost span for the guest's main thread is
opened once and never closes -- so a browser interval is largely unattributed
by construction, and a number without a denominator cannot say where a 670 ms
frame went. V8's sampling profiler covers the whole interval and every frame in
it, which is exactly the missing measurement.

WHAT IT PRINTS. Total samples (the denominator), then self time by function,
then self time grouped by the owner a name implies (translated guest block,
JIT translation, WebAssembly compilation, host import, renderer, idle). A
negative answer is printed too: a category that collected nothing prints zero,
so "the profiler never saw the JIT" cannot be mistaken for "the JIT is cheap".

THIS DOES NOT LAUNCH ANYTHING. WebLua owns the browser session; point this at
that session's Chrome profile directory while the run is in flight.
"""

from __future__ import annotations

import argparse
import collections
import json
import re
import sys
import time

from cdp_client import Cdp, CdpError, CdpTimeout, browser_endpoint, devtools_port
from cdp_console import attached_sessions

# THE URL DECIDES WHOSE CODE IT IS; THE SYMBOL SAYS WHICH PART.
#
# A release build strips the wasm name section, so an unresolved frame arrives
# as `wasm-function[7535]` and no name rule can tell the port's own compiled
# code from a translated guest block -- an earlier version of this file called
# both "wasm compile/instantiate", which is the one thing neither of them is.
# What separates them is the MODULE: the port is one module, `x2native.wasm`,
# and every translated block is its own anonymous module. Within the port
# module, load_symbols() supplies the name, and only then can dispatch be told
# from translation -- `x86p_jit_engine_run` EXECUTES blocks and a rule that
# bucketed every `x86p_jit*` symbol as translation would have reported the
# opposite of the truth.
_PORT_MODULE = "x2native.wasm"
_PORT_GLUE = "x2native.js"

# Checked against a symbol-map name, in order, for frames in the port module.
_PORT_OWNERS: tuple[tuple[str, tuple[str, ...]], ...] = (
    ("thread wait (spinning)", ("emscripten_futex", "__pthread_mutex", "pthread_cond", "_emscripten_yield")),
    ("x86port JIT translation", ("x86p_wasm_lower", "x86p_wasm_arena", "x86p_wasm_module", "x86p_jit_translate",
                                 "x86p_jit_storage", "x86p_decode", "jc_block")),
    ("JIT dispatch / execution", ("x86p_jit_engine_run", "x86p_jit_enter", "x86p_jit_lookup", "x86_engine_jit_",
                                  "x86p_sparse")),
    ("renderer", ("gpu_", "SDL_", "WEBGPU", "wgpu", "emscripten_webgpu")),
    ("host import stub / runtime", ("x86_", "x2_", "guest_", "d3d8_", "igt", "k32_")),
)

# Names that identify an owner whatever module they came from.
_BY_NAME: tuple[tuple[str, tuple[str, ...]], ...] = (
    ("wasm compile/instantiate", ("WebAssembly.Module", "WebAssembly.Instance", "Compile", "Instantiate")),
    ("idle / waiting", ("(program)", "(idle)", "(garbage collector)")),
)

# Every label categorize() can return, in the order the summary prints them.
_EVERY_CATEGORY: tuple[str, ...] = (
    "translated guest block",
    "JIT dispatch / execution",
    "x86port JIT translation",
    "wasm compile/instantiate",
    "thread wait (spinning)",
    "host import stub / runtime",
    "renderer",
    "port native code (unresolved)",
    "port native code (other)",
    "JS glue",
    "idle / waiting",
    "other",
)

# A thread parked in the pthread pool is not the program. Excluding these two
# is what separates the worker holding the guest from the fifteen waiting for
# it -- the sample COUNT does not, because V8 samples every target at the same
# rate and an idle worker produces just as many.
_NOT_WORKING: tuple[str, ...] = ("idle / waiting", "thread wait (spinning)")


def categorize(name: str, url: str) -> str:
    for label, patterns in _BY_NAME:
        for pattern in patterns:
            if pattern in name:
                return label
    if url.endswith(_PORT_MODULE):
        for label, patterns in _PORT_OWNERS:
            for pattern in patterns:
                if name.startswith(pattern):
                    return label
        # The symbol map did not cover this index. Said out loud rather than
        # folded into a neighbouring bucket: an unresolved share is how much of
        # the profile this tool cannot attribute, and it belongs in the output.
        if name.startswith("wasm-function["):
            return "port native code (unresolved)"
        return "port native code (other)"
    if url.endswith(_PORT_GLUE):
        return "JS glue"
    if name.startswith("wasm-function["):
        # A wasm frame from neither the port module nor its glue is a
        # translated guest block: those are the only other wasm modules the
        # page creates.
        return "translated guest block"
    return "other"


def pick_sessions(client: Cdp, want: str | None) -> list[tuple[str, dict]]:
    """Every session to profile.

    The port runs a pool of workers and does not say which one holds the guest,
    so the default is ALL of them: profiling one guess can only produce a
    confident measurement of an idle thread.

    The sample COUNT does not identify the busy one -- measured, 16 workers at
    ~57k samples each, because V8 samples an idle target exactly as often. What
    identifies it is samples that are not idle or parked, which is how
    summarize() ranks them.
    """
    sessions = attached_sessions(client)
    seen: set[str] = set()
    unique = []
    for session, target in sessions:
        if session in seen:
            continue
        seen.add(session)
        unique.append((session, target))
    if want:
        matching = [
            (s, t) for s, t in unique if want in t.get("url", "") or want == t.get("targetId")
        ]
        if not matching:
            raise CdpError(
                f"no target matched {want!r}; attached targets were: "
                + ", ".join(f"{t['type']} {t.get('url', '')}" for _, t in unique)
            )
        return matching
    workers = [(s, t) for s, t in unique if t["type"] in ("worker", "shared_worker")]
    if not workers:
        raise CdpError(
            "no dedicated worker is attached, so there is nothing to profile; attached: "
            + ", ".join(f"{t['type']} {t.get('url', '')}" for _, t in unique)
        )
    return workers


def list_targets(client: Cdp) -> int:
    sessions = attached_sessions(client)
    if not sessions:
        print("the browser attached NO targets at all")
        return 1
    # Two hex ids per row and no header cost a measurement: the session id was
    # read as the target id, --target refused sixteen times in a row, and the
    # loop around it reported sixteen blank results rather than one mistake.
    print(f"{'type':16s} {'session':32s}  {'--target':32s} url")
    for session, target in sessions:
        print(f"{target['type']:16s} {session}  {target['targetId']} {target.get('url', '')}")
    return 0


def profile_sessions(
    client: Cdp, sessions: list[tuple[str, dict]], seconds: float, interval_us: int
) -> list[tuple[dict, dict]]:
    """Run one profiler window across every session, then collect them all."""
    armed: list[tuple[str, dict]] = []
    unreachable: list[str] = []
    for session, target in sessions:
        try:
            client.call("Profiler.enable", session=session, timeout=5.0)
            client.call(
                "Profiler.setSamplingInterval", {"interval": interval_us}, session=session, timeout=5.0
            )
            client.call("Profiler.start", session=session, timeout=5.0)
        except CdpTimeout:
            # A worker parked in Atomics.wait cannot service the inspector. That
            # is a real fact about the run -- it says the thread is blocked, not
            # busy -- so it is named, never quietly dropped.
            unreachable.append(target["targetId"][:8])
            continue
        armed.append((session, target))
    if unreachable:
        print(
            f"{len(unreachable)} target(s) never answered the profiler and are blocked, "
            f"not sampled: {', '.join(unreachable)}"
        )
    if not armed:
        raise CdpError("every target was unreachable; nothing was sampled")
    time.sleep(seconds)
    collected = []
    for session, target in armed:
        try:
            collected.append(
                (client.call("Profiler.stop", session=session, timeout=20.0)["profile"], target)
            )
        except CdpTimeout:
            print(f"target {target['targetId'][:8]} blocked before its profile could be read back")
    return collected


def load_symbols(path) -> dict[int, str]:
    """`index:name` per line, as emscripten's --emit-symbol-map writes it.

    Without this a release profile is unreadable: the wasm name section is
    stripped, so every executing frame is `wasm-function[7535]` and the one
    number that matters cannot be attributed to anything. REFUSES an
    unreadable or empty map rather than returning {} and silently printing
    indices, because a profile that names nothing looks exactly like a profile
    of code with no hot function in it.
    """
    symbols: dict[int, str] = {}
    with open(path, "r", encoding="utf-8") as handle:
        for line in handle:
            index, _, name = line.strip().partition(":")
            if name and index.isdigit():
                symbols[int(index)] = name
    if not symbols:
        raise CdpError(f"symbol map {path} named nothing; a release profile would be unreadable")
    return symbols


_WASM_FRAME = re.compile(r"^wasm-function\[(\d+)\]$")


def resolve(name: str, url: str, symbols: dict[int, str]) -> str:
    """The symbol map covers ONE module, so only that module's frames may use
    it. A translated guest block's `wasm-function[44]` is index 44 of its own
    module and naming it from the port's map would invent a caller."""
    match = _WASM_FRAME.match(name)
    if match is None or not url.endswith(_PORT_MODULE):
        return name
    return symbols.get(int(match.group(1)), name)


def _self_samples(profile: dict) -> collections.Counter:
    if profile.get("samples"):
        return collections.Counter(profile["samples"])
    return collections.Counter(
        {node["id"]: node.get("hitCount", 0) for node in profile.get("nodes", [])}
    )


def tally(profile: dict, symbols: dict[int, str]) -> tuple[collections.Counter, collections.Counter, int]:
    """Self samples of one profile, by resolved name and by category."""
    by_name: collections.Counter = collections.Counter()
    by_category: collections.Counter = collections.Counter()
    nodes = {node["id"]: node for node in profile.get("nodes", [])}
    samples = _self_samples(profile)
    for node_id, hits in samples.items():
        node = nodes.get(node_id)
        if node is None:
            by_name["<unknown node>"] += hits
            by_category["other"] += hits
            continue
        frame = node["callFrame"]
        url = frame.get("url", "")
        name = resolve(frame.get("functionName") or "(anonymous)", url, symbols)
        by_name[f"{name}  [{url.rsplit('/', 1)[-1]}]"] += hits
        by_category[categorize(name, url)] += hits
    return by_name, by_category, sum(samples.values())


def working_samples(by_category: collections.Counter) -> int:
    return sum(hits for label, hits in by_category.items() if label not in _NOT_WORKING)


def _print_shares(by_category: collections.Counter, by_name: collections.Counter, total: int, top: int) -> None:
    # Every category is printed whether or not it collected anything, so a zero
    # reads as "looked, found none" and not as "never looked".
    print("\nself time by category:")
    for label in _EVERY_CATEGORY:
        count = by_category.get(label, 0)
        print(f"  {100.0 * count / total:6.2f}%  {count:8d}  {label}")
    unclassified = set(by_category) - set(_EVERY_CATEGORY)
    if unclassified:
        raise AssertionError(f"categorize() produced labels the summary does not print: {sorted(unclassified)}")
    print(f"\ntop {top} by self time:")
    for name, count in by_name.most_common(top):
        print(f"  {100.0 * count / total:6.2f}%  {count:8d}  {name}")


def summarize(profiles: list[tuple[dict, dict]], top: int, symbols: dict[int, str]) -> None:
    by_name: collections.Counter = collections.Counter()
    by_category: collections.Counter = collections.Counter()
    per_worker: list[tuple[int, int, str, collections.Counter, collections.Counter]] = []
    total = 0
    window = 0.0
    for profile, target in profiles:
        names, categories, count = tally(profile, symbols)
        total += count
        window = max(window, (profile.get("endTime", 0) - profile.get("startTime", 0)) / 1e6)
        per_worker.append((working_samples(categories), count, target["targetId"][:8], names, categories))
        by_name.update(names)
        by_category.update(categories)

    busy = sum(1 for _, count, _, _, _ in per_worker if count)
    print(
        f"\n{len(profiles)} target(s) profiled over {window:.2f} s; {busy} collected any sample. "
        f"Total samples {total} (the denominator for every share below)."
    )
    # Ranked by samples that are NOT idle or parked, because that -- and not
    # the sample count -- is what identifies the thread doing the work.
    for working, count, label, _, _ in sorted(per_worker, reverse=True):
        print(f"  worker {label}: {count} sample(s), {working} working")
    if total == 0:
        print(
            "ZERO samples across every target -- no JavaScript or WebAssembly ran "
            "in this window at all, which is a fact about the run, not a tool failure"
        )
        return

    print("\nEVERY TARGET TOGETHER")
    _print_shares(by_category, by_name, total, top)

    # The aggregate above is dominated by however many workers happen to be
    # parked, so its percentages say more about the pool size than about the
    # program. The busiest target's own breakdown is the one with a meaningful
    # denominator, and it is printed whether or not it found anything.
    working, count, label, names, categories = max(per_worker)
    print(
        f"\nBUSIEST TARGET {label}: {working} working sample(s) of its {count} "
        f"({100.0 * working / count if count else 0.0:.1f}% of its wall time). "
        "Shares below are against ITS OWN samples, so its idle time is visible "
        "rather than hidden in the pool's."
    )
    if not working:
        print(
            "  NOTHING was working on any target in this window. That is a fact "
            "about the run -- every thread was parked or idle -- not a tool failure."
        )
        return
    _print_shares(categories, names, count, top)


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument(
        "--profile-dir",
        default="scratch/weblua-local/chrome-profile",
        help="the Chrome profile directory WebLua was given (holds DevToolsActivePort)",
    )
    parser.add_argument("--port", type=int, default=0, help="DevTools port, if already known")
    parser.add_argument("--list", action="store_true", help="list targets and exit")
    parser.add_argument("--target", default=None, help="substring of the target URL, or a target id")
    parser.add_argument("--seconds", type=float, default=10.0)
    parser.add_argument("--interval-us", type=int, default=200, help="V8 sampling interval")
    parser.add_argument("--top", type=int, default=30)
    parser.add_argument("--save", default=None, help="write the raw .cpuprofile here as well")
    parser.add_argument(
        "--symbols",
        default="build/web/x2native.js.symbols",
        help="emscripten --emit-symbol-map output for the module being profiled",
    )
    args = parser.parse_args(argv)

    port = args.port or devtools_port(args.profile_dir)
    client = Cdp(browser_endpoint(port))
    try:
        if args.list:
            return list_targets(client)
        sessions = pick_sessions(client, args.target)
        print(f"profiling {len(sessions)} target(s) for {args.seconds:.0f}s", flush=True)
        profiles = profile_sessions(client, sessions, args.seconds, args.interval_us)
        if args.save:
            with open(args.save, "w", encoding="utf-8") as handle:
                json.dump([{"target": t, "profile": p} for p, t in profiles], handle)
            print(f"raw profiles written to {args.save}")
        summarize(profiles, args.top, load_symbols(args.symbols))
        return 0
    finally:
        client.close()


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
