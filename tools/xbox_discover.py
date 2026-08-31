#!/usr/bin/env python3
"""Drive the optional Xbox runtime discovery loop to a fixed point."""

from __future__ import annotations

import fcntl
import json
import os
from pathlib import Path
import re
import subprocess
import sys


ROOT = Path(__file__).resolve().parents[1]


def configured_path(name: str, default: Path) -> Path:
    value = os.environ.get(name)
    if not value:
        return default
    path = Path(value).expanduser()
    return path if path.is_absolute() else (ROOT / path).resolve()


def first(pattern: str, text: str) -> str | None:
    match = re.search(pattern, text, re.MULTILINE)
    return match.group(1) if match else None


def containing_function(path: Path, address: int) -> tuple[int, int] | None:
    records = json.loads(path.read_text())
    if not isinstance(records, list):
        raise ValueError(f"expected a list of functions, got {type(records).__name__}")
    checked = 0
    for record in records:
        try:
            start = int(record["start"], 16)
            end = int(record["end"], 16)
        except (KeyError, TypeError, ValueError):
            continue
        checked += 1
        if start < address < end:
            return start, end
    if not checked:
        raise ValueError("no readable function records")
    return None


def append_seed(path: Path, value: str, round_number: int) -> None:
    records = json.loads(path.read_text())
    if any(record["start"].casefold() == value.casefold() for record in records):
        return
    records.append({
        "start": value,
        "name": f"icall_{value[2:]}",
        "why": "Indirect-call target with no static reference, found by "
               f"tools/xbox_discover.py round {round_number} from the ICALL miss tally.",
    })
    path.write_text(json.dumps(records, indent=2) + "\n")


def main() -> int:
    rounds = int(os.environ.get("ROUNDS", "8"))
    if rounds <= 0:
        raise SystemExit("xbox_discover: ROUNDS must be positive")
    build = configured_path("BUILD_DIR", ROOT / "build/xbox")
    run_directory = configured_path("RUN_DIR", ROOT / "scratch/run/xbox")
    seeds = ROOT / "xbox/seeds.json"
    functions = configured_path(
        "XBOXRECOMP_FUNCS",
        ROOT / "vendor/xboxrecomp/tools/disasm/output/functions.json",
    )
    logs = ROOT / "scratch/logs"
    logs.mkdir(parents=True, exist_ok=True)
    log_path = logs / "xbox_discover.log"
    lock_path = ROOT / "scratch/.xbox_discover.lock"
    lock_path.parent.mkdir(parents=True, exist_ok=True)

    with lock_path.open("a+", encoding="utf-8") as lock:
        try:
            fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
        except BlockingIOError:
            lock.seek(0)
            holder = lock.read().strip() or "<lock held but unnamed>"
            raise SystemExit("xbox_discover: another discovery loop is running; "
                             f"lock {lock_path}, holder {holder}") from None
        lock.seek(0)
        lock.truncate()
        lock.write(f"pid {os.getpid()}\n")
        lock.flush()

        seen: set[str] = set()
        with log_path.open("w", encoding="utf-8") as log:
            def report(message: str) -> None:
                print(message)
                print(message, file=log, flush=True)

            for round_number in range(1, rounds + 1):
                report(f"=== round {round_number}: build + run ===")
                build_result = subprocess.run(
                    ["cmake", "--build", str(build), "--target", "xml2_xbox_recomp",
                     "-j", str(os.cpu_count() or 1)],
                    cwd=ROOT, stdout=log, stderr=subprocess.STDOUT,
                )
                if build_result.returncode:
                    raise SystemExit(f"round {round_number}: BUILD FAILED -- see {log_path}")

                run_log = logs / f"xbox_discover_run_{round_number}.log"
                try:
                    run = subprocess.run(
                        [str(build / "xml2_xbox_recomp")], cwd=run_directory,
                        capture_output=True, timeout=300,
                    )
                    output = (run.stdout + run.stderr).decode(errors="replace")
                    status = run.returncode
                except subprocess.TimeoutExpired as error:
                    output = ((error.stdout or b"") + (error.stderr or b"")).decode(
                        errors="replace")
                    status = 124
                run_log.write_text(output)
                calls = first(r"^\[ICALL\] (\d+) indirect calls", output) or "0"
                kernel = first(r"^\[KERNEL\] (\d+) kernel calls total", output) or "unreported"
                report(f"round {round_number}: exit={status}, {calls} indirect calls, "
                       f"{kernel} kernel calls")

                tail = first(r"UNRESOLVED-TAIL-JUMP VA (0x[0-9A-F]+)", output)
                if tail:
                    report(f"round {round_number}: STOP -- first miss is a TAIL JUMP ({tail}); "
                           "fix its switch table rather than manufacturing a function")
                    return 1
                value = first(r"\bUNRESOLVED VA (0x[0-9A-F]+)", output)
                if not value:
                    skipped = first(r"range-skipped VA (0x[0-9A-F]+)", output)
                    if skipped:
                        report(f"round {round_number}: STOP -- first miss is OUT-OF-IMAGE "
                               f"({skipped}); this is corrupt state, not a seed")
                    else:
                        report(f"round {round_number}: DONE -- every indirect call resolved")
                    return 0
                if value in seen:
                    report(f"round {round_number}: STOP -- {value} returned after seeding")
                    return 1
                seen.add(value)

                try:
                    containing = containing_function(functions, int(value, 16))
                except (OSError, ValueError, json.JSONDecodeError) as error:
                    report(f"round {round_number}: cannot check whether {value} is mid-function: "
                           f"{error}; seeding requires manual verification")
                    return 1
                if containing:
                    report(f"round {round_number}: STOP -- {value} is inside existing function "
                           f"0x{containing[0]:08X}..0x{containing[1]:08X}")
                    return 1

                report(f"round {round_number}: seeding {value}")
                append_seed(seeds, value, round_number)
                environment = os.environ.copy()
                environment["XBOX_DISCOVER_PID"] = str(os.getpid())
                relift = subprocess.run([sys.executable, str(ROOT / "tools/xbox_relift.py")], cwd=ROOT,
                                        env=environment, stdout=log,
                                        stderr=subprocess.STDOUT)
                if relift.returncode:
                    raise SystemExit(f"round {round_number}: RE-LIFT FAILED -- see {log_path}")
            report(f"stopped after {rounds} rounds without converging")
            return 1


if __name__ == "__main__":
    raise SystemExit(main())
