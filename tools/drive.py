#!/usr/bin/env python3
"""Print and validate the shared automated-driving profiles.

Usage:
    tools/drive.py port       print the native X2_INPUT_SCRIPT profile
    tools/drive.py stock      print the Wine-control X2_KEYS profile
    tools/drive.py count      count press events (profile defaults to port)
    tools/drive.py --check    prove both profiles are non-empty and well formed
"""

from __future__ import annotations

import sys
from typing import Mapping, Sequence


# Native input is scheduled in guest frames. These ranges are measured game
# timing, not machine-specific timing.
PORT_SCRIPT = (
    "f2600-2900/50:Return,f3150-3260/40:Escape,"
    "f4044+40:Down,f4135+40:Return"
)

# The Wine control can only schedule wall-clock seconds, so its windows are
# deliberately coarser than the native profile.
STOCK_KEYS = "195-300/12:Return,380-500/20:Return"

PROFILES = {"port": PORT_SCRIPT, "stock": STOCK_KEYS}


def profile(side: str) -> str:
    try:
        return PROFILES[side]
    except KeyError as exc:
        raise ValueError(f"unknown drive profile: {side}") from exc


def entries(script: str) -> list[str]:
    return [entry for entry in script.replace(",", " ").split() if entry]


def count_events(script: str) -> int:
    """Count native frame events using the shipping script parser's shape."""
    count = 0
    for entry in entries(script):
        parts = entry.split(":")
        if len(parts) != 2:
            continue
        spec = parts[0]
        if not spec.startswith("f"):
            count += 1
            continue

        spec = spec[1:]
        bounds = spec.split("-")
        if len(bounds) == 2 and "/" in bounds[1]:
            end_step = bounds[1].split("/")
            try:
                start = float(bounds[0])
                end = float(end_step[0])
                step = float(end_step[1])
            except (ValueError, IndexError):
                pass
            else:
                if step > 0 and end >= start:
                    count += int((end - start) / step) + 1
                    continue
        count += 1
    return count


def validate_profiles(profiles: Mapping[str, str]) -> tuple[list[str], list[str]]:
    reports: list[str] = []
    errors: list[str] = []
    for side in ("port", "stock"):
        script = profiles.get(side, "")
        windows = entries(script)
        if not windows:
            errors.append(
                f"drive: the {side} profile is EMPTY -- a run using it would be\n"
                "       undriven, and an undriven run photographs whatever the\n"
                "       intro happens to be showing."
            )
            continue
        bad = sum(":" not in entry for entry in windows)
        if bad:
            errors.append(
                f"drive: {bad} entr(y/ies) in the {side} profile have no ':'"
            )
        else:
            reports.append(
                f"drive: {side} -- {len(windows)} press window(s): {script}"
            )
    return reports, errors


def usage() -> str:
    return "\n".join((__doc__ or "").strip().splitlines()[2:7])


def main(argv: Sequence[str] | None = None) -> int:
    arguments = list(sys.argv[1:] if argv is None else argv)
    command = arguments[0] if arguments else ""
    if command in PROFILES:
        sys.stdout.write(profile(command))
        return 0
    if command == "count":
        side = arguments[1] if len(arguments) > 1 else "port"
        try:
            script = profile(side)
        except ValueError:
            print(usage())
            return 2
        print(count_events(script))
        return 0
    if command == "--check":
        reports, errors = validate_profiles(PROFILES)
        for line in reports:
            print(line)
        for line in errors:
            print(line)
        return 1 if errors else 0
    print(usage())
    return 2


if __name__ == "__main__":
    raise SystemExit(main())
