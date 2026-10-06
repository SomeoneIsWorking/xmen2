#!/usr/bin/env python3
"""Rank guest functions by the host time a perf profile spent in their JIT code.

A run started with `--set jit.map=<path>` writes one line per published
translation: "HOST SIZE guest_EIP" (hex, perf's map format). This tool charges
every sample in perf.data to the block whose host range holds it, and names the
block by the nearest export at or below it in the module the run log says was
mapped there. The main executable exports nothing, so its blocks are named
`module+offset`.

A code-region flush hands the same host bytes to later translations, so a newer
range EVICTS every older one it overlaps. Keeping the older one at its own start
address charged hot samples to one-time registration code in the first use.

    tools/jit_map_profile.py --map scratch/jitdump/jit.map \\
        --perf-data scratch/jitdump/perf.data --log scratch/jitdump/run.log \\
        --game-dir "$GAME_PC_DIR" [--top 30] [--blocks] [--time START,END]
    tools/jit_map_profile.py --selftest
"""
import argparse
import bisect
import collections
import os
import re
import subprocess
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

# How far past an export a block may sit and still be named after it; beyond
# this the name would be a guess.
EXPORT_REACH = 0x4000


class Ranges:
    """Live host ranges of translations, newest wins where they overlap."""

    def __init__(self):
        self.starts = []
        self.info = {}  # start -> (size, guest eip)
        self.evicted = 0

    def add(self, host, size, guest):
        i = bisect.bisect_left(self.starts, host)
        if i > 0 and self.starts[i - 1] + self.info[self.starts[i - 1]][0] > host:
            i -= 1
        j = i
        while j < len(self.starts) and self.starts[j] < host + size:
            j += 1
        for start in self.starts[i:j]:
            del self.info[start]
        self.evicted += j - i
        del self.starts[i:j]
        self.starts.insert(i, host)
        self.info[host] = (size, guest)

    def guest_at(self, ip):
        i = bisect.bisect_right(self.starts, ip) - 1
        if i < 0:
            return None
        size, guest = self.info[self.starts[i]]
        return guest if ip < self.starts[i] + size else None


def parse_map(lines):
    ranges = Ranges()
    for number, line in enumerate(lines, 1):
        fields = line.split()
        if len(fields) != 3 or not fields[2].startswith("guest_"):
            sys.exit(f"jit_map_profile: map line {number} is not 'HOST SIZE guest_EIP': {line!r}")
        ranges.add(int(fields[0], 16), int(fields[1], 16), int(fields[2][6:], 16))
    return ranges


def attribute(ips, ranges):
    """Samples per guest block, and how many samples no block owns."""
    by_block = collections.Counter()
    unowned = 0
    for ip in ips:
        guest = ranges.guest_at(ip)
        if guest is None:
            unowned += 1
        else:
            by_block[guest] += 1
    return by_block, unowned


class Names:
    """Nearest export at or below a guest address, per mapped module."""

    def __init__(self, modules, export_table):
        self.modules = sorted(modules)  # (base, name)
        self.table = sorted(export_table)  # (address, "module!export")
        self.keys = [address for address, _ in self.table]

    def name(self, guest):
        i = bisect.bisect_right(self.keys, guest) - 1
        if i >= 0 and guest - self.keys[i] < EXPORT_REACH:
            module = self.table[i][1].split("!")[0]
            if self.module_of(guest) == module:
                return self.table[i][1]
        module = self.module_of(guest)
        if module is None:
            return f"{guest:#010x}"
        base = next(b for b, n in self.modules if n == module)
        return f"{module}+{guest - base:#x}"

    def module_of(self, guest):
        bases = [b for b, _ in self.modules]
        i = bisect.bisect_right(bases, guest) - 1
        return self.modules[i][1] if i >= 0 else None


def mapped_modules(log_text):
    return [(int(m.group(2), 16), m.group(1))
            for m in re.finditer(r"mapped (\S+)\s+at (0x[0-9a-f]+)", log_text)]


def export_table(modules, game_dir):
    from pe import PE

    table = []
    for base, module in modules:
        path = os.path.join(game_dir, module)
        if not os.path.exists(path):
            continue
        for _, rva, kind, name, _ in PE(path).exports() or []:
            if kind == "CODE" and name:
                table.append((base + rva, f"{module}!{name}"))
    return table


def sample_ips(perf_data, time_range=None):
    window = ["--time", time_range] if time_range else []
    out = subprocess.run(["perf", "script", "-G", "-i", perf_data, "-F", "ip", *window],
                         capture_output=True, text=True, check=True).stdout
    return [int(word, 16) for word in out.split()]


def report(ips, ranges, names, top, blocks):
    by_block, unowned = attribute(ips, ranges)
    total = len(ips)
    owned = sum(by_block.values())
    print(f"{total} sample(s); {owned} ({100 * owned / max(total, 1):.1f}%) in "
          f"{len(by_block)} guest block(s) of {len(ranges.starts)} live range(s) "
          f"({ranges.evicted} older range(s) evicted by reused host bytes); "
          f"{unowned} outside every block (host code, or JIT bytes no block owns)")
    rank = collections.Counter()
    for guest, count in by_block.items():
        rank[names.name(guest) if not blocks else f"{guest:#010x} {names.name(guest)}"] += count
    for label, count in rank.most_common(top):
        print(f"{100 * count / max(total, 1):6.2f}% {label}")


def selftest():
    ranges = parse_map([
        "1000 100 guest_00400000",  # old block, overwritten after a flush
        "2000 40 guest_00500000",
        "1080 20 guest_00600000",  # reuses 0x1080..0x10a0: evicts the old block
    ])
    by_block, unowned = attribute([0x1010, 0x1085, 0x2010, 0x3000], ranges)
    failures = []
    if by_block[0x00400000]:
        failures.append("a range a newer one overlapped still owns samples")
    if by_block[0x00600000] != 1 or by_block[0x00500000] != 1:
        failures.append(f"owned samples were misattributed: {dict(by_block)}")
    if unowned != 2:
        failures.append(f"{unowned} unowned sample(s), expected 2 (the evicted range's and one past every range)")
    names = Names([(0x00400000, "a.exe"), (0x10000000, "b.dll")],
                  [(0x10001000, "b.dll!f"), (0x10009000, "b.dll!g")])
    if names.name(0x10001010) != "b.dll!f":
        failures.append("an address inside an export was not named after it")
    if names.name(0x10006000) != "b.dll+0x6000":
        failures.append("an address past an export's reach was named after it")
    if names.name(0x00401234) != "a.exe+0x1234":
        failures.append("an exe address was not named module+offset")
    for failure in failures:
        print(f"jit_map_profile selftest: FAIL {failure}")
    if not failures:
        print("jit_map_profile selftest: passed (eviction, unowned samples, naming)")
    return 1 if failures else 0


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--map")
    parser.add_argument("--perf-data")
    parser.add_argument("--log", help="the run's log, for where each module was mapped")
    parser.add_argument("--game-dir", help="the directory holding the mapped modules")
    parser.add_argument("--top", type=int, default=30)
    parser.add_argument("--blocks", action="store_true", help="rank blocks, not functions")
    parser.add_argument("--time", help="only samples in this perf time window, START,END "
                        "in perf's own seconds (as `perf script -F time` prints them)")
    parser.add_argument("--selftest", action="store_true")
    args = parser.parse_args()
    if args.selftest:
        return selftest()
    for flag in ("map", "perf_data", "log", "game_dir"):
        if not getattr(args, flag):
            parser.error(f"--{flag.replace('_', '-')} is required")
    with open(args.map) as f:
        ranges = parse_map(f.read().splitlines())
    if not ranges.starts:
        sys.exit(f"jit_map_profile: {args.map} names no translation -- was the run started with --set jit.map?")
    with open(args.log, errors="replace") as f:
        modules = mapped_modules(f.read())
    if not modules:
        sys.exit(f"jit_map_profile: {args.log} says no module was mapped")
    names = Names(modules, export_table(modules, args.game_dir))
    report(sample_ips(args.perf_data, args.time), ranges, names, args.top, args.blocks)
    return 0


if __name__ == "__main__":
    sys.exit(main())
