#!/usr/bin/env python3
"""Capture and characterize one off-screen frame from build/native/x2native."""

from __future__ import annotations

import argparse
import os
from pathlib import Path
import re
import subprocess
import time

from PIL import Image

from tools.run import load_dotenv


ROOT = Path(__file__).resolve().parents[1]


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("seconds", nargs="?", type=int, default=45)
    parser.add_argument("name", nargs="?", default="native")
    return parser.parse_args()


def validate_request(seconds: int, name: str) -> None:
    if seconds <= 0 or not name or Path(name).name != name:
        raise ValueError("seconds must be positive and name must be one filename")


def main() -> int:
    args = parse_args()
    try:
        validate_request(args.seconds, args.name)
    except ValueError as error:
        raise SystemExit(f"native_shot: {error}") from error
    load_dotenv()
    if not os.environ.get("GAME_PC_DIR"):
        raise SystemExit("native_shot: set GAME_PC_DIR in .env (see .env.example)")
    binary = ROOT / "build/native/x2native"
    if not os.access(binary, os.X_OK):
        raise SystemExit(f"native_shot: {binary} is not built -- captured NOTHING")

    screenshots = ROOT / "scratch/screenshots"
    logs = ROOT / "scratch/logs"
    screenshots.mkdir(parents=True, exist_ok=True)
    logs.mkdir(parents=True, exist_ok=True)
    ppm = screenshots / f"{args.name}.ppm"
    png = screenshots / f"{args.name}.png"
    log = logs / f"{args.name}.log"
    for stale in (ppm, png):
        if stale.is_file():
            stale.unlink()

    environment = os.environ.copy()
    environment.update({
        "X2_SHOT": str(ppm),
        "X2_UNPACED": environment.get("X2_UNPACED", "1"),
        "X2_HEARTBEAT": environment.get("X2_HEARTBEAT", "10"),
    })
    with log.open("wb") as output:
        process = subprocess.Popen(
            [str(binary), "--no-window", "--d3d8", "--run"],
            cwd=ROOT, env=environment, stdout=output, stderr=subprocess.STDOUT,
        )
        waited = 0
        while waited < args.seconds and process.poll() is None:
            time.sleep(2)
            waited += 2
            text = log.read_text(errors="replace")
            if ppm.is_file() and ppm.stat().st_size:
                if re.search(r"presents \d+ \(\+[1-9]\d*", text):
                    break
        ended = process.poll() is not None
        if not ended:
            time.sleep(4)
            process.terminate()
            try:
                process.wait(timeout=1)
            except subprocess.TimeoutExpired:
                process.kill()
                process.wait()

    if not ppm.is_file() or ppm.stat().st_size == 0:
        tail = "\n".join(log.read_text(errors="replace").splitlines()[-6:])
        raise SystemExit(f"native_shot: no frame was written to {ppm}\n{tail}")
    if ended:
        print(f"native_shot: run ended after {waited}s; characterizing its last frame")

    with Image.open(ppm) as source:
        image = source.convert("RGB")
        image.save(png)
        colours = image.getcolors(maxcolors=1 << 24) or []
        colours.sort(reverse=True)
        top = colours[0] if colours else (0, (0, 0, 0))
        percentage = 100.0 * top[0] / (image.width * image.height)
        print(f"native_shot: {png}  {image.width}x{image.height}  "
              f"{len(colours)} distinct colour(s); the most common is "
              f"{top[1]} at {percentage:.1f}%")
        if len(colours) < 4 or percentage > 99.0:
            print("  THAT IS ONE FLAT COLOUR -- nothing was drawn into the frame.")
            return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
