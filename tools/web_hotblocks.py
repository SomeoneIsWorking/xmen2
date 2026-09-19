#!/usr/bin/env python3
"""Differences the engine's hot-block heartbeat into per-window shares.

The heartbeat prints a cumulative histogram: every table is every block entry
since the run began. Read one of those tables directly and a route's opening --
the loader, the first frames, whatever ran before the camera settled -- is
averaged into every later reading, and a cluster that costs a fifth of the
present is reported as costing an eighth. Issue #165's headline was wrong that
way once, from exactly this mistake, which is why the differencing is a tool now
and not a calculation done again by hand.

So: consecutive tables are subtracted, and every share is a share of that
window's own entries.

The tables are the top forty rows, not the whole histogram, and a block may
enter or leave that forty between two snapshots. A row absent from the earlier
table has NO known earlier count -- it had somewhere between zero and the
fortieth row's total -- so its delta is unknown rather than equal to its new
count. Those rows are excluded and COUNTED, and the count is printed, because a
window silently missing half a cluster reads exactly like a cluster that got
cheaper.

The denominator is the header's own total, which covers all 61,000-odd blocks
rather than the printed forty, so a share here is a share of every block the
guest entered.
"""

import argparse
import re
import statistics
import sys
from pathlib import Path

HEADER = re.compile(
    r"JIT hot blocks: (\d+) distinct, (\d+) entries total, (\d+) key\(s\) dropped"
)
ROW = re.compile(r"\s(\d+)\.\s+0x([0-9a-fA-F]{8})\s+(.*?)\s+(\d+)\s+[\d.]+%\s*$")
STAMP = re.compile(r"\[(\d{4}-\d\d-\d\dT[\d:.]+Z)\]")
EMPTY = "recorded no block entry"


class Snapshot:
    """One heartbeat table: the run-total denominator and the printed rows."""

    def __init__(self, total, distinct, stamp):
        self.total = total
        self.distinct = distinct
        self.stamp = stamp
        self.rows = {}
        self.names = {}

    def add(self, address, name, entries):
        self.rows[address] = entries
        self.names[address] = name


def seconds(stamp):
    """Wall-clock seconds from an ISO stamp, for elapsed within one run."""
    if stamp is None:
        return None
    clock = stamp.split("T")[1].rstrip("Z")
    hours, minutes, rest = clock.split(":")
    return int(hours) * 3600 + int(minutes) * 60 + float(rest)


def snapshots(text):
    """Every complete table in a console log, oldest first."""
    found = []
    empty = 0
    current = None
    for line in text.splitlines():
        if EMPTY in line:
            empty += 1
            current = None
            continue
        head = HEADER.search(line)
        if head:
            stamp = STAMP.search(line)
            current = Snapshot(
                int(head.group(2)), int(head.group(1)), stamp.group(1) if stamp else None
            )
            found.append(current)
            continue
        row = ROW.search(line)
        if row and current is not None:
            current.add(int(row.group(2), 16), row.group(3).strip(), int(row.group(4)))
    return found, empty


class Window:
    """The difference between two consecutive tables."""

    def __init__(self, before, after):
        self.entries = after.total - before.total
        self.elapsed = None
        start, end = seconds(before.stamp), seconds(after.stamp)
        if start is not None and end is not None:
            self.elapsed = end - start
        self.deltas = {}
        self.unknown = []
        for address, count in after.rows.items():
            if address in before.rows:
                self.deltas[address] = count - before.rows[address]
            else:
                self.unknown.append(address)

    def share(self, low, high):
        """This window's share for one address range, and what it could not see.

        Returns None when a row of the range appeared from outside the printed
        forty, because the honest answer then is that this window cannot say.
        """
        blind = [a for a in self.unknown if low <= a <= high]
        if blind:
            return None, len(blind)
        total = sum(d for a, d in self.deltas.items() if low <= a <= high)
        if self.entries <= 0:
            return None, 0
        return 100.0 * total / self.entries, 0


def parse_range(text):
    low, _, high = text.partition("-")
    if not high:
        raise argparse.ArgumentTypeError(f"expected LOW-HIGH, got {text!r}")
    return int(low, 16), int(high, 16)


def report(text, ranges, out=sys.stdout):
    tables, empty = snapshots(text)
    if not tables:
        print(
            "web_hotblocks: refusing -- no hot-block table in this log. Looked for "
            f"{HEADER.pattern!r}. The heartbeat prints one only when jit.profile "
            "armed the histogram: launch with ?arg=--set&arg=jit.profile=65536.",
            file=out,
        )
        if empty:
            print(
                f"         ({empty} heartbeat(s) DID report the histogram armed and "
                "empty, so the run reached the report and executed no translated "
                "block -- that is a different failure)",
                file=out,
            )
        return 1
    if len(tables) < 2:
        print(
            "web_hotblocks: refusing -- 1 table in this log and differencing needs "
            "two. A single table is cumulative over the whole run, which is the "
            "reading this tool exists to avoid. Capture a longer log.",
            file=out,
        )
        return 1

    windows = [Window(tables[i], tables[i + 1]) for i in range(len(tables) - 1)]
    print(
        f"{len(tables)} table(s), {len(windows)} window(s); "
        f"{tables[-1].distinct} distinct blocks, "
        f"{tables[-1].total - tables[0].total} entries across the whole span",
        file=out,
    )

    for low, high in ranges:
        print(f"\n0x{low:08x}-0x{high:08x}", file=out)
        shares, blind, dead = [], 0, 0
        for index, window in enumerate(windows):
            value, missing = window.share(low, high)
            if missing:
                blind += 1
                mark = f"     -- {missing} row(s) entered the top 40, cannot difference"
            elif value is None:
                dead += 1
                mark = "     -- no block entered in this window"
            else:
                shares.append(value)
                mark = f"{value:8.2f}%"
            elapsed = f"{window.elapsed:5.1f}s" if window.elapsed is not None else "    ?"
            print(
                f"  {index + 1:3d}  {elapsed}  {window.entries:12d} entries  {mark}",
                file=out,
            )
        if not shares:
            print(
                "  no window could measure this range: every one of them either had "
                "a row arrive from outside the printed forty or entered no blocks at "
                "all. This is not a share of zero.",
                file=out,
            )
            continue
        print(
            f"  {len(shares)} usable: min {min(shares):.2f}%  "
            f"median {statistics.median(shares):.2f}%  max {max(shares):.2f}%"
            + (f"  ({blind} window(s) blind, {dead} idle)" if blind or dead else ""),
            file=out,
        )
    return 0


def movers(text, count, out=sys.stdout):
    """The blocks that grew most over the whole span, by windowed share."""
    tables, _ = snapshots(text)
    if len(tables) < 2:
        return 1
    span = Window(tables[0], tables[-1])
    ranked = sorted(span.deltas.items(), key=lambda kv: -kv[1])[:count]
    print(f"\ntop {len(ranked)} by growth over the span ({span.entries} entries):", file=out)
    for address, delta in ranked:
        print(
            f"  0x{address:08x} {tables[-1].names.get(address, ''):<38} "
            f"{delta:12d}  {100.0 * delta / span.entries:6.2f}%",
            file=out,
        )
    if span.unknown:
        print(
            f"  ({len(span.unknown)} row(s) of the final table were outside the first "
            "table's top 40 and cannot be differenced)",
            file=out,
        )
    return 0


def selftest(out=sys.stdout):
    """Both refusals and the two readings that must not be confused."""
    failures = 0

    def check(condition, what):
        nonlocal failures
        if not condition:
            print(f"FAIL {what}", file=out)
            failures += 1

    import io

    def run(text, ranges=()):
        buffer = io.StringIO()
        report(text, ranges, buffer)
        return buffer.getvalue()

    check("refusing" in run("nothing here"), "a log with no table refuses")
    check(
        "armed and empty" in run(f"[engine] [HB] JIT hot blocks: {EMPTY} ever"),
        "an armed-and-empty log says so rather than claiming no instrument",
    )

    def table(stamp, total, rows):
        head = (
            f"log [{stamp}] [engine] [HB] JIT hot blocks: 7 distinct, {total} "
            "entries total, 0 key(s) dropped (table full), top 2 follows"
        )
        body = [
            f"log [{stamp}] [engine] [HB] {i + 1:2d}. 0x{a:08x} {n:<40} {c:10d}   1.0%"
            for i, (a, n, c) in enumerate(rows)
        ]
        return "\n".join([head, *body])

    one = table("2026-01-01T00:00:00.0Z", 1000, [(0x2E047470, "unnamed", 100)])
    check("refusing" in run(one) and "needs" in run(one), "one table refuses to guess")

    # The reading that matters: a cluster flat in cumulative terms while the
    # window says it doubled. Cumulative shares are 10% then 15%; the window is
    # 200 of 1000 new entries, which is 20%.
    two = one + "\n" + table("2026-01-01T00:00:05.0Z", 2000, [(0x2E047470, "unnamed", 300)])
    text = run(two, [(0x2E047470, 0x2E04861E)])
    check("20.00%" in text, "the share is of the window, not of the run")
    check("10" not in text.split("median")[-1], "the cumulative share is not reported")

    # A row that arrives from outside the printed forty must not read as growth.
    three = one + "\n" + table(
        "2026-01-01T00:00:05.0Z", 2000, [(0x2E047470, "unnamed", 300), (0x2E048500, "u", 900)]
    )
    text = run(three, [(0x2E047470, 0x2E04861E)])
    check("cannot difference" in text, "an unseen row is named, not counted as zero")
    check(
        "no window could measure" in text,
        "a blind-only range refuses instead of reporting a share",
    )

    print(f"web_hotblocks selftest: {failures} failure(s)", file=out)
    return 1 if failures else 0


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("log", nargs="?", type=Path)
    parser.add_argument(
        "--range",
        dest="ranges",
        action="append",
        type=parse_range,
        default=[],
        metavar="LOW-HIGH",
        help="hex guest address range to total, repeatable",
    )
    parser.add_argument("--movers", type=int, default=0)
    parser.add_argument("--selftest", action="store_true")
    args = parser.parse_args()
    if args.selftest:
        return selftest()
    if args.log is None:
        parser.error("a console log is required unless --selftest")
    text = args.log.read_text(errors="replace")
    status = report(text, args.ranges)
    if args.movers:
        movers(text, args.movers)
    return status


if __name__ == "__main__":
    sys.exit(main())
