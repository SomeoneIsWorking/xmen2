#!/usr/bin/env python3
"""Run the optional Xbox recompilation from its required game working directory."""

from __future__ import annotations

import os
from pathlib import Path
import subprocess
import sys


ROOT = Path(__file__).resolve().parents[1]


def resolve_path(value: str | None, default: Path, *, base: Path = ROOT) -> Path:
    path = Path(value).expanduser() if value else default
    return path if path.is_absolute() else (base / path).resolve()


def publish_game_link(run_directory: Path, game: Path) -> None:
    link = run_directory / "game"
    if link.is_symlink():
        link.unlink()
    elif link.exists():
        raise SystemExit(f"xbox_run: refusing to replace non-symlink {link}")
    link.symlink_to(game, target_is_directory=True)


def main() -> int:
    build = resolve_path(os.environ.get("BUILD"), ROOT / "build/xbox")
    game = resolve_path(os.environ.get("GAME"), ROOT / "scratch/xbox_iso")
    run_directory = resolve_path(os.environ.get("RUNDIR"), ROOT / "scratch/run/xbox")
    log = resolve_path(os.environ.get("LOG"), ROOT / "scratch/logs/xbox_run.log",
                       base=Path.cwd())
    executable = build / "xml2_xbox_recomp"
    if not os.access(executable, os.X_OK):
        raise SystemExit(f"xbox_run: no binary at {executable} -- build it first:\n"
                         f"  cmake --build {build}")
    if not (game / "default.xbe").is_file():
        raise SystemExit(f"xbox_run: no default.xbe under {game}; provide the "
                         "copyrighted game yourself or set GAME")
    run_directory.mkdir(parents=True, exist_ok=True)
    log.parent.mkdir(parents=True, exist_ok=True)
    publish_game_link(run_directory, game)
    print(f"xbox_run: {executable}  (cwd {run_directory}, game -> {game})")
    print(f"xbox_run: log {log}")
    with log.open("wb") as destination:
        process = subprocess.Popen([str(executable), *sys.argv[1:]], cwd=run_directory,
                                   stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        assert process.stdout is not None
        for line in iter(process.stdout.readline, b""):
            destination.write(line)
            destination.flush()
            sys.stdout.buffer.write(line)
            sys.stdout.buffer.flush()
        status = process.wait()
    print(f"xbox_run: exit {status}")
    return status


if __name__ == "__main__":
    raise SystemExit(main())
