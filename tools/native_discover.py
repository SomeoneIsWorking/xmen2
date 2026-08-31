#!/usr/bin/env python3
"""Recover statically hidden native targets until the runtime reaches a fixed point."""

from __future__ import annotations

import argparse
import os
from pathlib import Path
import re
import shlex
import subprocess
import sys

from tools.run import load_dotenv


ROOT = Path(__file__).resolve().parents[1]
RECOMP = ROOT / "scratch/recomp"
LOGS = ROOT / "scratch/logs"
GENERATED = ROOT / "src/recomp"
BUILD = ROOT / "build/native"
MODULE_SUFFIXES = (".ark", ".vtab", ".iat")


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("max_rounds", nargs="?", type=int, default=8)
    arguments = parser.parse_args()
    if arguments.max_rounds <= 0:
        parser.error("max_rounds must be positive")
    return arguments


def command(arguments: list[str], *, capture: bool = False,
            output: Path | None = None, environment: dict[str, str] | None = None,
            timeout: int | None = None) -> subprocess.CompletedProcess[str]:
    if output is not None:
        output.parent.mkdir(parents=True, exist_ok=True)
        with output.open("w", encoding="utf-8") as destination:
            return subprocess.run(arguments, cwd=ROOT, env=environment, text=True,
                                  stdout=destination, stderr=subprocess.STDOUT,
                                  timeout=timeout, check=False)
    return subprocess.run(arguments, cwd=ROOT, env=environment, text=True,
                          capture_output=capture, timeout=timeout, check=False)


def invoke(arguments: list[str], *, tail: int = 0) -> subprocess.CompletedProcess[str]:
    result = command(arguments, capture=True)
    lines = (result.stdout + result.stderr).splitlines()
    selected = lines[-tail:] if tail else lines
    if selected:
        print("\n".join(selected))
    return result


def parse_candidate_count(text: str, label: str) -> int:
    match = re.search(rf"{re.escape(label)}: (\d+)", text)
    return int(match.group(1)) if match else 0


def parse_missing_targets(text: str) -> list[tuple[str, str]]:
    targets = {
        match.groups()
        for match in re.finditer(
            r"^    ([A-Za-z0-9_]+\.(?:dll|exe)) +(0x[0-9A-Fa-f]+)",
            text,
            re.MULTILINE,
        )
    }
    return sorted(targets)


def non_trace_tail(text: str, count: int) -> list[str]:
    return [line for line in text.splitlines() if not line.startswith("[TRACE]")][-count:]


def exported_modules() -> list[str]:
    modules = []
    for export in sorted(RECOMP.glob("*.json")):
        name = export.stem
        if not name.endswith(MODULE_SUFFIXES):
            modules.append(name)
    return modules


def remove_generated(module: str) -> None:
    direct = GENERATED / f"{module}.c"
    if direct.is_file():
        direct.unlink()
    for split in GENERATED.glob(f"{module}_[0-9][0-9][0-9].c"):
        split.unlink()


def regenerate(module: str, split_size: int) -> None:
    export = RECOMP / f"{module}.json"
    remove_generated(module)
    emitted = invoke([
        sys.executable, "tools/recomp.py", "emit", str(export),
        str(GENERATED / f"{module}.c"), "--split", str(split_size),
    ], tail=1)
    if emitted.returncode:
        raise SystemExit(f"native_discover: emission failed for {module}")
    native = command([
        sys.executable, "tools/recomp.py", "native", str(export),
        str(GENERATED / f"{module}_native.c"),
    ])
    if native.returncode:
        raise SystemExit(f"native_discover: native dispatch failed for {module}")


def ghidra(module: str, flag: str, source: Path, tail: int) -> bool:
    result = invoke(["tools/ghidra_export.sh", module, flag, str(source)], tail=tail)
    return result.returncode == 0


def build_native(context: str) -> None:
    log = LOGS / "discover-build.log"
    result = command([
        "cmake", "--build", str(BUILD), "--target", "x2native",
        "-j", str(os.cpu_count() or 1),
    ], output=log)
    if result.returncode:
        raise SystemExit(f"native_discover: {context} failed, see {log.relative_to(ROOT)}")


def seed_bulk(module: str, split_size: int) -> bool:
    export = RECOMP / f"{module}.json"
    changed = False
    reloc = RECOMP / f"{module}.reloc"
    reloc_log = RECOMP / f"{module}.reloc.log"
    result = command([
        sys.executable, "tools/seed_relocs.py", str(export), "-o", str(reloc),
    ], output=reloc_log)
    if result.returncode == 0:
        count = parse_candidate_count(reloc_log.read_text(errors="replace"),
                                      "CANDIDATE function starts")
        if count:
            print(f"== bulk: {module} has {count} relocation-derived candidate(s) to seed")
            if not ghidra(module, "--seed", reloc, 2):
                raise SystemExit(f"native_discover: Ghidra relocation seed failed for {module}")
            changed = True
    else:
        reason = reloc_log.read_text(errors="replace").splitlines()
        print(f"== bulk: {module} -- no relocation seeding: "
              f"{reason[-1] if reason else 'tool produced no diagnostic'}")
        dataptr = RECOMP / f"{module}.dataptr"
        dataptr_log = RECOMP / f"{module}.dataptr.log"
        result = command([
            sys.executable, "tools/seed_data_ptrs.py", str(export), "-o", str(dataptr),
        ], output=dataptr_log)
        if result.returncode == 0:
            count = parse_candidate_count(dataptr_log.read_text(errors="replace"),
                                          "CANDIDATE function starts")
            if count:
                print(f"== bulk: {module} has {count} data-pointer candidate(s) to seed")
                if not ghidra(module, "--seed", dataptr, 2):
                    raise SystemExit(f"native_discover: Ghidra data-pointer seed failed for {module}")
                changed = True
        else:
            reason = dataptr_log.read_text(errors="replace").splitlines()
            print(f"== bulk: {module} -- no data-pointer seeding: "
                  f"{reason[-1] if reason else 'tool produced no diagnostic'}")

    immediate = RECOMP / f"{module}.codeimm"
    result = command([
        sys.executable, "tools/seed_code_imms.py", str(export), "-o", str(immediate),
    ], capture=True)
    if result.returncode:
        raise SystemExit(f"native_discover: code-immediate scan failed for {module}")
    count = parse_candidate_count(result.stdout, "NEW function starts to seed")
    if count:
        print(f"== bulk: {module} has {count} code-immediate function start(s) to seed")
        if not ghidra(module, "--seed", immediate, 1):
            raise SystemExit(f"native_discover: Ghidra code-immediate seed failed for {module}")
        changed = True
    if changed:
        regenerate(module, split_size)
    return changed


def bulk_pass(split_size: int) -> None:
    modules = exported_modules()
    if not modules:
        raise SystemExit("native_discover: no exports in scratch/recomp -- there is NOTHING\n"
                         "  to bulk-seed, and that is a broken tree, not an empty result.")
    print(f"== bulk: seeding {len(modules)} exported module(s)")
    for module in modules:
        seed_bulk(module, split_size)
    build_native("bulk-seed rebuild")


def run_round(binary: Path, arguments: list[str], timeout_seconds: int,
              environment: dict[str, str], raw: Path) -> int:
    try:
        return command([str(binary), "--no-window", *arguments], output=raw,
                       environment=environment, timeout=timeout_seconds).returncode
    except subprocess.TimeoutExpired:
        return 124


def report_empty(round_number: int, status: int, text: str, binary: Path,
                 run_arguments: list[str], timeout_seconds: int) -> int:
    invocation = " ".join([str(binary), "--no-window", *run_arguments])
    if "constructor targets" in text:
        print(f"native_discover: round {round_number} reported missing targets but none\n"
              "  parsed -- the report format changed and this loop is BLIND.", file=sys.stderr)
        return 1
    if status in {124, 137, 143}:
        if re.search(r"presents [0-9]+ \(\+[1-9]", text):
            print(f"native_discover: round {round_number} found no missing constructor targets\n"
                  f"  on the path taken by: {invocation}\n"
                  f"  The run was still RENDERING when the {timeout_seconds}s limit hit.\n"
                  "  The blind spot is TIME; raise RUN_TIMEOUT to look further. Last frames:")
            for line in re.findall(r"^.*presents [0-9]+ \(\+[1-9].*$", text, re.MULTILINE)[-2:]:
                print(line)
            return 0
        print(f"native_discover: round {round_number} did NOT converge -- the run was\n"
              f"  still going after {timeout_seconds}s and was killed (exit {status}),\n"
              "  and it was NOT presenting frames. It found no missing targets UP TO\n"
              "  THAT POINT, which is not the same as there being none.", file=sys.stderr)
        for line in non_trace_tail(text, 5):
            print(line, file=sys.stderr)
        return 2
    print(f"native_discover: round {round_number} found no missing constructor targets\n"
          f"  on the path taken by: {invocation}\n"
          "  Targets reachable only under OTHER arguments were never executed.\n"
          f"  The run ended by itself (exit {status}). Last output:")
    for line in non_trace_tail(text, 3):
        print(line)
    return 0


def diagnose_repeat(round_number: int, targets: list[tuple[str, str]]) -> int:
    print(f"native_discover: round {round_number} asked for exactly the same targets as\n"
          "  the round before, so the seeding did NOT take. This is a stuck loop.",
          file=sys.stderr)
    for module in sorted({module for module, _ in targets}):
        base = Path(module).stem
        log = LOGS / f"ghidra-{base}.log"
        if not log.is_file():
            print(f"    {base}: NO ghidra log at {log}", file=sys.stderr)
            continue
        additions = [line for line in log.read_text(errors="replace").splitlines()
                     if line.startswith("ADD:")][-4:]
        print(f"    --- {base} ({log})", file=sys.stderr)
        for line in additions:
            print(f"      {line}", file=sys.stderr)
    return 1


def seed_round(targets: list[tuple[str, str]], split_size: int) -> None:
    modules = sorted({module for module, _ in targets})
    print(f"== round: {len(targets)} missing target(s) in {' '.join(modules)}")
    for module in modules:
        base = Path(module).stem
        seeds = RECOMP / f"{base}.seeds"
        seeds.write_text("".join(f"{address}\n" for name, address in targets if name == module))
        if not ghidra(base, "--seed", seeds, 1):
            raise SystemExit(f"native_discover: Ghidra seed failed for {base}")
        log = LOGS / f"ghidra-{base}.log"
        split_values = sorted(set(re.findall(
            r"^ADD: 0x(0x[0-9a-f]+) already inside a function",
            log.read_text(errors="replace") if log.is_file() else "",
            re.MULTILINE,
        )))
        if split_values:
            splits = RECOMP / f"{base}.autosplit"
            splits.write_text("".join(f"{value}\n" for value in split_values))
            print(f"   escalating {len(split_values)} seed(s) to a split -- each CARVES "
                  "an existing function; inspect the containing function for an SEH prologue")
            invoke([sys.executable, "tools/whose_function.py",
                    str(RECOMP / f"{base}.json"), str(splits)])
            if not ghidra(base, "--split-at", splits, 2):
                raise SystemExit(f"native_discover: Ghidra split failed for {base}")
        regenerate(base, split_size)
    build_native("build")


def main() -> int:
    args = parse_args()
    os.chdir(ROOT)
    load_dotenv()
    if not os.environ.get("GAME_PC_DIR"):
        raise SystemExit("native_discover: set GAME_PC_DIR in .env")
    binary = BUILD / "x2native"
    if not os.access(binary, os.X_OK):
        raise SystemExit(f"native_discover: {binary} is not built -- discovered NOTHING")
    watched = os.environ.get("X2_ARGS", "")
    if watched and not watched.startswith("-"):
        print(f"native_discover: X2_ARGS is the runtime argument watch ({watched!r}), "
              "not this tool's command line; use RUN_ARGS instead", file=sys.stderr)
    verification = command([sys.executable, "tools/verify_export.py"], capture=True)
    if verification.returncode:
        print("native_discover: an export does not match its shipped binary --",
              file=sys.stderr)
        print(verification.stdout + verification.stderr, file=sys.stderr, end="")
        return 2

    split_size = int(os.environ.get("SPLIT", "1500"))
    if os.environ.get("SKIP_BULK", "0") != "1":
        bulk_pass(split_size)
    run_arguments = []
    if os.environ.get("RUN", "1"):
        run_arguments.append("--run")
    run_arguments.extend(shlex.split(os.environ.get("RUN_ARGS", "--d3d8")))
    timeout_seconds = int(os.environ.get("RUN_TIMEOUT", "300"))
    environment = os.environ.copy()
    environment.setdefault("X2_HEARTBEAT", "5")
    environment.setdefault("X2_UNPACED", "1")
    previous: list[tuple[str, str]] = []
    for round_number in range(1, args.max_rounds + 1):
        raw = RECOMP / ".discover.seeds.raw"
        status = run_round(binary, run_arguments, timeout_seconds, environment, raw)
        text = raw.read_text(errors="replace") if raw.is_file() else ""
        targets = parse_missing_targets(text)
        (RECOMP / ".discover.seeds").write_text(
            "".join(f"{module} {address}\n" for module, address in targets))
        if not targets:
            return report_empty(round_number, status, text, binary,
                                run_arguments, timeout_seconds)
        if targets == previous:
            return diagnose_repeat(round_number, targets)
        previous = targets
        print(f"== round {round_number}: {len(targets)} missing target(s)")
        seed_round(targets, split_size)
    print(f"native_discover: stopped after {args.max_rounds} rounds with targets still "
          "outstanding.\n  That is a cap, not a conclusion -- re-run to continue.",
          file=sys.stderr)
    return 1


if __name__ == "__main__":
    raise SystemExit(main())
