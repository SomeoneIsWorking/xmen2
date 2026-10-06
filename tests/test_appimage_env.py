#!/usr/bin/env python3
"""Prove packaged setup cannot inherit a developer checkout's .env."""

from __future__ import annotations

import os
from pathlib import Path
import subprocess
import sys
import tempfile


ROOT = Path(__file__).resolve().parents[1]


def main() -> int:
    if len(sys.argv) != 2:
        raise SystemExit("usage: test_appimage_env.py <x2native>")
    binary = Path(sys.argv[1]).resolve()
    raw = ROOT / "scratch/raw"
    raw.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="appimage-env-test-", dir=raw) as directory:
        working = Path(directory)
        sentinel = "/sentinel/appimage-env-must-not-load"
        (working / ".env").write_text(
            f"GAME_PC_DIR={sentinel}\nX2_ENV_FILE_PROBE=packaged-leak\n",
            encoding="utf-8",
        )
        environment = os.environ.copy()
        environment.pop("GAME_PC_DIR", None)
        environment["SDL_VIDEODRIVER"] = "dummy"
        result = subprocess.run(
            [str(binary), "--appimage", "--no-window", "--selftest"],
            cwd=working,
            env=environment,
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            timeout=30,
        )
    if result.returncode != 77:
        raise AssertionError(
            f"packaged no-install probe returned {result.returncode}:\n{result.stdout}"
        )
    forbidden = ("loaded ", sentinel, "packaged-leak")
    for text in forbidden:
        if text in result.stdout:
            raise AssertionError(f"AppImage inherited project .env marker {text!r}")
    if "GAME_PC_DIR is unset" not in result.stdout:
        raise AssertionError(f"no-install refusal was not observed:\n{result.stdout}")
    print("appimage env: packaged setup ignored developer .env")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
