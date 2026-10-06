#!/usr/bin/env python3
"""Attribute a Gecko (Firefox-family) CPU profile of the game tab's content process.

WHY THIS EXISTS. The startup profiler documented in project state S021
profiled the PARENT process, and the port's work is not there: the guest's x86
blocks, the x86port JIT and the host import stubs run on pthread workers the
page created, and those threads live in the CONTENT process, on the tab the
game is running in. A profile that never contained them could only say "the
parent is busy", which is not a place to fix anything. This module reads the
profile that process produced -- Gecko's own JSON, started from Marionette's
content context -- and turns it into a per-thread sample share and a top-30 of
the busiest guest worker's self and total functions.

WHAT IT REUSES. Frame naming and owner categorisation are NOT re-derived here:
`wasm-function[7535]` in a Gecko profile is exactly the frame `web_profile.py`
meets in a V8 one, so the same symbol map (`build/web/x2native.js.symbols`),
the same one-module-only resolver and the same owner categories apply, and they
are imported from that owner rather than copied. Only the Gecko table-walking
is local.

THE NEGATIVE IS DESIGNED. A profile with no samples at all, a profile whose
busiest thread has none (nothing ran), and a warm-up-only sample set each name
themselves and exit non-zero rather than printing an empty table. Every share
carries its denominator, and a name the symbol map did not cover is counted and
printed as unresolved instead of being folded into a neighbour.
"""

from __future__ import annotations

import argparse
import collections
import json
import sys
from pathlib import Path
from typing import Any

sys.path.insert(0, str(Path(__file__).resolve().parent))
from web_profile import categorize, load_symbols, resolve, working_samples

# A Gecko thread the port can never run on. Named so a profile of the wrong
# process is recognised as such rather than reported as a slow port.
_NOT_GUEST: tuple[str, ...] = ("GeckoMain", "Compositor", "Renderer", "Socket Thread")

# A profile of one process covers its own threads; this is how many of them the
# program may be spread over before the busiest one is a real answer.
_MIN_THREADS_REPORTED = 30


class ProfileError(RuntimeError):
    """The profile is unreadable, empty, or contains no work to attribute."""


class Thread:
    """One Gecko thread's self and total sample tallies, already resolved."""

    def __init__(self, name: str, tid: int, process_type: str) -> None:
        self.name = name
        self.tid = tid
        self.process_type = process_type
        self.self_by_func: collections.Counter = collections.Counter()
        self.total_by_func: collections.Counter = collections.Counter()
        self.by_category: collections.Counter = collections.Counter()
        self.samples = 0
        self.scanned = 0
        self.skipped = 0
        self.unresolved_leaf = 0

    @property
    def working(self) -> int:
        return working_samples(self.by_category)

    @property
    def label(self) -> str:
        return f"{self.name or '(unnamed)'} tid {self.tid} ({self.process_type or 'unknown'})"

def _column(table: Any, name: str) -> list:
    """One column of a Gecko table; empty when this build's version omits it."""
    if isinstance(table, dict) and isinstance(table.get(name), list):
        return table[name]
    return []


def _sample_rows(thread: dict) -> tuple[list, list]:
    """(times, stacks) for one thread, tolerating a thread that never sampled.

    Gecko changed the sample shape more than once (a bare stack index in older
    builds, a leaf-first array of stack indices in current ones), so both are
    read here rather than being assumed; a thread with no `samples` at all is
    a fact about the profile, not a reason to stop.
    """
    samples = thread.get("samples")
    if not isinstance(samples, dict):
        return [], []
    data = samples.get("data")
    if not isinstance(data, list):
        return [], []
    times = samples.get("time") if isinstance(samples.get("time"), list) else []
    rows: list = []
    for sample in data:
        if isinstance(sample, list):
            rows.append(sample)
        elif isinstance(sample, int):
            rows.append([sample])
        else:
            rows.append([])
    if not times:
        times = [0] * len(rows)
    return times, rows


def _func_of_frame(frame_index: int, frame_table: dict, func_table: dict,
                   string_table: list) -> tuple[int, str, str] | None:
    """(func id, name, url) for a frame index, or None when it does not exist."""
    funcs = _column(frame_table, "func")
    if not 0 <= frame_index < len(funcs):
        return None
    func_id = funcs[frame_index]
    names = _column(func_table, "name")
    files = _column(func_table, "fileName")
    if not 0 <= func_id < len(names):
        return None
    raw = names[func_id]
    name = string_table[raw] if isinstance(raw, int) and 0 <= raw < len(string_table) else str(raw)
    raw_file = files[func_id] if 0 <= func_id < len(files) else ""
    url = string_table[raw_file] if isinstance(raw_file, int) and 0 <= raw_file < len(string_table) else ""
    return func_id, name, url


def thread_rows(profile: dict, symbols: dict[int, str], since: float,
                start: float) -> list[Thread]:
    """Every thread in the profile, tallied from samples at or after `since`."""
    threads = profile.get("threads")
    if not isinstance(threads, list) or not threads:
        raise ProfileError("this file has no `threads` array, so it is not a Gecko profile")
    rows: list[Thread] = []
    for raw in threads:
        if not isinstance(raw, dict):
            continue
        row = Thread(str(raw.get("name") or ""), int(raw.get("tid") or 0),
                     str(raw.get("processType") or ""))
        frame_table = raw.get("frameTable") or {}
        func_table = raw.get("funcTable") or {}
        stack_table = raw.get("stackTable") or {}
        strings = raw.get("stringTable") or []
        stack_frames = _column(stack_table, "frame")
        times, samples = _sample_rows(raw)
        row.scanned = len(samples)
        for time_us, stack in zip(times, samples, strict=True):
            if since > 0.0 and (time_us - start) / 1e6 < since:
                continue
            if not stack:
                row.skipped += 1
                continue
            row.samples += 1
            # Leaf first: the first entry is the sample's self time.
            funcs: list[tuple[int, str, str]] = []
            for stack_index in stack:
                if not 0 <= stack_index < len(stack_frames):
                    continue
                found = _func_of_frame(stack_frames[stack_index], frame_table, func_table, strings)
                if found is not None:
                    funcs.append(found)
            if not funcs:
                row.skipped += 1
                continue
            leaf_id, leaf_name, leaf_url = funcs[0]
            leaf_name = resolve(leaf_name, leaf_url, symbols)
            if leaf_name.startswith("wasm-function[") and leaf_url.endswith("x2native.wasm"):
                row.unresolved_leaf += 1
            label = f"{leaf_name}  [{leaf_url.rsplit('/', 1)[-1] or '?'}]"
            row.self_by_func[label] += 1
            row.by_category[categorize(leaf_name, leaf_url)] += 1
            for _func_id, func_name, func_url in funcs:
                key = f"{func_name}  [{func_url.rsplit('/', 1)[-1] or '?'}]"
                row.total_by_func[key] += 1
        if row.samples:
            rows.append(row)
    return rows


def busiest(rows: list[Thread]) -> Thread:
    """The guest worker doing the work, which is not the one with most samples.

    Ranked by samples that are neither idle nor parked, for the same reason
    `web_profile.py` ranks its targets that way: a sampler visits a waiting
    worker as often as a working one, so sample COUNT says more about the pool
    size than about the program.
    """
    guests = [row for row in rows if row.name not in _NOT_GUEST]
    pool = guests or rows
    if not pool:
        raise ProfileError("no thread in this profile collected a sample")
    return max(pool, key=lambda row: (row.working, row.samples))


def _print_table(title: str, counts: collections.Counter, total: int, top: int) -> None:
    print(f"\n{title} (share of the thread's {total} sample(s)):")
    if not counts:
        print("  (none)")
        return
    for name, count in counts.most_common(top):
        print(f"  {100.0 * count / total:6.2f}%  {count:8d}  {name}")


def report(profile: dict, symbols: dict[int, str], symbols_path: Path, top: int,
           since: float) -> int:
    """Print the attribution table; non-zero when there is nothing to attribute."""
    meta = profile.get("meta") or {}
    start = float(meta.get("startTime") or 0.0)
    end = float(meta.get("endTime") or 0.0)
    rows = thread_rows(profile, symbols, since, start)
    scanned = sum(row.scanned for row in rows)
    if not rows:
        raise ProfileError(
            f"scanned {scanned} sample(s) and resolved 0 threads: this profile holds "
            "no working samples, so it attributes nothing"
            + (f" (it starts at {since:.0f}s and everything is inside that warm-up)"
               if since > 0.0 else ""))
    worker = busiest(rows)
    if not worker.samples:
        raise ProfileError("the busiest thread has 0 samples, so this attributes nothing")
    leaf_total = sum(worker.self_by_func.values())
    resolved = leaf_total - worker.unresolved_leaf

    print(f"gecko profile: process {meta.get('processType') or '?'} "
          f"version {meta.get('version') or '?'}, "
          f"{(end - start) / 1e6:.1f}s window, {len(rows)} thread(s) sampled")
    if since > 0.0:
        print(f"  samples before the {since:.0f}s warm-up are excluded from every share below")
    print(f"  scanned {scanned} sample(s), resolved {resolved} of the busiest thread's "
          f"{leaf_total} with {symbols_path.name}")
    if worker.unresolved_leaf:
        print(f"  {worker.unresolved_leaf} leaf sample(s) stayed wasm-function[N]: they are "
              "port code the symbol map does not cover, and are reported as such")

    print("\nsample share by thread (working = neither idle nor parked):")
    total = sum(row.samples for row in rows)
    for row in sorted(rows, key=lambda r: (r.working, r.samples), reverse=True):
        print(f"  {100.0 * row.samples / total:6.2f}%  {row.samples:8d} sample(s)  "
              f"{row.working:8d} working  {row.label}  leaf {_leaf_name(row)}")
    if len(rows) > _MIN_THREADS_REPORTED:
        print(f"  (the pool holds more threads than this profile sampled: {len(rows)} here)")

    print(f"\nBUSIEST GUEST WORKER {worker.label}: {worker.samples} sample(s), "
          f"{worker.working} working ({100.0 * worker.working / worker.samples:.1f}% "
          "of its wall time)")
    for label in sorted(worker.by_category):
        print(f"  {100.0 * worker.by_category[label] / worker.samples:6.2f}%  "
              f"{worker.by_category[label]:8d}  {label}")
    _print_table(f"top {top} by self time", worker.self_by_func, worker.samples, top)
    _print_table(f"top {top} by total time", worker.total_by_func, worker.samples, top)
    return 0


def _leaf_name(row: Thread) -> str:
    return row.self_by_func.most_common(1)[0][0].split("  [")[0] if row.self_by_func else "-"


def summarize_file(path: Path, symbols: Path, top: int, since: float) -> int:
    try:
        profile = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, ValueError) as error:
        raise ProfileError(f"{path} is not readable JSON: {error}") from error
    return report(profile, load_symbols(symbols), symbols, top, since)


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("profile", type=Path, help="a Gecko profile JSON file")
    parser.add_argument("--symbols", type=Path, default=Path("build/web/x2native.js.symbols"))
    parser.add_argument("--top", type=int, default=30)
    parser.add_argument("--since", type=float, default=0.0,
                        help="ignore samples in the first N seconds of the profile")
    args = parser.parse_args(argv)
    try:
        return summarize_file(args.profile, args.symbols, args.top, args.since)
    except (ProfileError, RuntimeError) as error:
        print(f"gecko_profile: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
