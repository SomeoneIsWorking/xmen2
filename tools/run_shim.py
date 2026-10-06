#!/usr/bin/env python3
"""Run a staged retail X-Men 2 build under Wine and capture bounded evidence."""

from __future__ import annotations

import argparse
from collections.abc import Mapping, Sequence
from dataclasses import dataclass
import os
from pathlib import Path
import random
import shlex
import shutil
import subprocess
import sys
import threading
import time

from PIL import Image

try:
    from tools.build_stocklog import BuildRefusal, load_environment
except ModuleNotFoundError:
    from build_stocklog import BuildRefusal, load_environment


DEFAULT_MUTE = "winepulse.drv,winealsa.drv,wineoss.drv,winecoreaudio.drv=d"
DEFAULT_ICD = (
    "/usr/share/vulkan/icd.d/lvp_icd.i686.json:/usr/share/vulkan/icd.d/lvp_icd.x86_64.json"
)


@dataclass(frozen=True)
class RunPaths:
    run_dir: Path
    log: Path
    screenshot: Path


def resolve_paths(root: Path, name: str, executable: str) -> RunPaths:
    if not name or Path(name).name != name:
        raise BuildRefusal("run-directory name must be one path component")
    run_dir = root / "scratch" / "run" / name
    if not (run_dir / executable).is_file():
        raise BuildRefusal(f"{run_dir / executable} missing -- ran NOTHING")
    return RunPaths(
        run_dir=run_dir,
        log=root / "scratch" / "logs" / f"{name}.log",
        screenshot=root / "scratch" / "screenshots" / f"{name}.png",
    )


def select_display() -> int:
    candidates = list(range(90, 98))
    random.SystemRandom().shuffle(candidates)
    for number in candidates:
        if not Path(f"/tmp/.X11-unix/X{number}").exists():
            return number
    raise BuildRefusal("Xvfb displays :90 through :97 are all occupied")


def wait_for_x_server(process: subprocess.Popen[bytes], display: int) -> None:
    socket = Path(f"/tmp/.X11-unix/X{display}")
    deadline = time.monotonic() + 5.0
    while time.monotonic() < deadline:
        if socket.exists():
            return
        if process.poll() is not None:
            raise BuildRefusal(f"Xvfb :{display} exited with {process.returncode}")
        time.sleep(0.05)
    raise BuildRefusal(f"Xvfb :{display} did not create its control socket")


def parse_key_events(specification: str) -> list[tuple[float, str]]:
    events: list[tuple[float, str]] = []
    for item in filter(None, specification.split(",")):
        try:
            timing, key = item.split(":", 1)
            if "-" in timing and "/" in timing:
                bounds, step_text = timing.split("/", 1)
                start_text, end_text = bounds.split("-", 1)
                start, end, step = int(start_text), int(end_text), int(step_text)
                if step <= 0 or end < start:
                    raise ValueError
                events.extend((float(at), key) for at in range(start, end + 1, step))
            else:
                events.append((float(timing), key))
        except ValueError as exc:
            raise BuildRefusal(f"invalid X2_KEYS event: {item!r}") from exc
        if not key:
            raise BuildRefusal(f"invalid X2_KEYS event: {item!r}")
    return sorted(events)


def visible_window(display_env: Mapping[str, str]) -> str | None:
    result = subprocess.run(
        ["xdotool", "search", "--onlyvisible", "--name", ".*"],
        env=display_env,
        capture_output=True,
        text=True,
        check=False,
    )
    windows = result.stdout.split()
    return windows[-1] if windows else None


def drive_keys(
    events: Sequence[tuple[float, str]],
    display_env: Mapping[str, str],
    stop: threading.Event,
) -> None:
    start = time.monotonic()
    for at, key in events:
        if stop.wait(max(0.0, start + at - time.monotonic())):
            return
        window = visible_window(display_env)
        if window is None:
            print(f"run_shim: KEY {key} at t={at:g}s NOT SENT -- no visible window")
            continue
        command = ["xdotool", "key", "--clearmodifiers", "--window", window, key]
        result = subprocess.run(command, env=display_env, check=False)
        outcome = "sent" if result.returncode == 0 else "FAILED"
        print(f"run_shim: KEY {key} {outcome} at t={at:g}s (window {window})")


def capture_samples(
    display_env: Mapping[str, str], destination: Path, seconds: float, count: int
) -> list[Path]:
    if count <= 0:
        raise BuildRefusal("X2_SAMPLES must be positive")
    samples: list[Path] = []
    start = time.monotonic()
    for index in range(1, count + 1):
        deadline = start + seconds * index / count
        time.sleep(max(0.0, deadline - time.monotonic()))
        sample = Path(f"{destination}.{index}")
        result = subprocess.run(
            ["import", "-window", "root", f"png:{sample}"],
            env=display_env,
            check=False,
        )
        if result.returncode == 0 and sample.is_file():
            samples.append(sample)
        else:
            print(f"SHOT {index}: capture FAILED -- shows NOTHING")
    return samples


def choose_sample(samples: Sequence[Path], destination: Path) -> None:
    measured: list[tuple[int, float, Image.Image, Path]] = []
    for sample in samples:
        with Image.open(sample) as source:
            image = source.convert("RGB")
        colors = image.getcolors(maxcolors=1 << 24) or []
        dominant = max((count for count, _ in colors), default=image.width * image.height)
        fraction = dominant / (image.width * image.height)
        index = sample.suffix.removeprefix(".")
        print(
            f"SHOT {index}: distinct_colors={len(colors)} dominant {fraction * 100:.1f}%"
            + ("  <-- uniform" if fraction > 0.995 else "")
        )
        measured.append((len(colors), fraction, image, sample))
    if not measured:
        raise BuildRefusal("every sample failed -- this run proves nothing")
    color_count, fraction, image, _ = max(measured, key=lambda item: item[0])
    image.save(destination)
    suffix = "  <-- ALL SAMPLES UNIFORM: nothing rendered" if fraction > 0.995 else ""
    print(f"SHOT: kept sample with {color_count} colours as {destination}{suffix}")


def stop_process(process: subprocess.Popen[bytes], timeout: float = 2.0) -> str:
    if process.poll() is not None:
        return str(process.returncode)
    process.terminate()
    try:
        process.wait(timeout=timeout)
    except subprocess.TimeoutExpired:
        process.kill()
        process.wait(timeout=timeout)
    return "running"


def run(root: Path, name: str, seconds: float, inherited_env: Mapping[str, str]) -> None:
    env = load_environment(root, inherited_env)
    executable = env.get("X2_EXE", "XMen2.exe")
    paths = resolve_paths(root, name, executable)
    prefix = Path(env.get("WINEPREFIX") or env.get("WINE_PREFIX", ""))
    if not str(prefix) or str(prefix) == ".":
        raise BuildRefusal("set WINE_PREFIX in .env (see .env.example)")
    if not prefix.is_dir():
        raise BuildRefusal(f"WINEPREFIX {prefix} missing")
    for tool in ("Xvfb", "wine", "winepath", "import"):
        if shutil.which(tool) is None:
            raise BuildRefusal(f"required tool {tool} is missing")

    paths.log.parent.mkdir(parents=True, exist_ok=True)
    paths.screenshot.parent.mkdir(parents=True, exist_ok=True)
    display = select_display()
    resolution = env.get("X2_RES", "800x600")
    xvfb = subprocess.Popen(
        ["Xvfb", f":{display}", "-screen", "0", f"{resolution}x24"],
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
    )
    wine: subprocess.Popen[bytes] | None = None
    key_stop = threading.Event()
    try:
        wait_for_x_server(xvfb, display)
        run_env = dict(env)
        run_env.update(
            {
                "DISPLAY": f":{display}",
                "WINEPREFIX": str(prefix),
                "WINEDEBUG": env.get("X2_WINEDEBUG", "+loaddll"),
                "WINEDLLOVERRIDES": (
                    f"d3d8,d3d9={env.get('X2_D3D', 'n')};{env.get('X2_MUTE', DEFAULT_MUTE)}"
                ),
                "VK_DRIVER_FILES": env.get("X2_VK_ICD", DEFAULT_ICD),
                "VK_ICD_FILENAMES": env.get("X2_VK_ICD", DEFAULT_ICD),
                "X2_TRACE": env.get("X2_TRACE", ""),
            }
        )
        path_result = subprocess.run(
            ["winepath", "-w", f"./{executable}"],
            cwd=paths.run_dir,
            env=run_env,
            capture_output=True,
            text=True,
            check=False,
        )
        if path_result.returncode != 0 or not path_result.stdout.strip():
            raise BuildRefusal("winepath could not resolve the staged executable")
        command = [
            "wine",
            "explorer",
            f"/desktop=x2,{resolution}",
            path_result.stdout.strip(),
            *shlex.split(env.get("RUN_ARGS", "")),
        ]
        with paths.log.open("wb") as log:
            wine = subprocess.Popen(
                command,
                cwd=paths.run_dir,
                env=run_env,
                stdout=log,
                stderr=subprocess.STDOUT,
            )
            events = parse_key_events(env.get("X2_KEYS", ""))
            if events:
                print(f"run_shim: X2_KEYS is set -- this run is DRIVEN: {env['X2_KEYS']}")
                threading.Thread(
                    target=drive_keys,
                    args=(events, run_env, key_stop),
                    daemon=True,
                ).start()
            else:
                print("run_shim: X2_KEYS is unset -- NOTHING drives this run")
            samples = capture_samples(
                run_env,
                paths.screenshot,
                seconds,
                int(env.get("X2_SAMPLES", "3")),
            )
            wrapper_exit = stop_process(wine)
        key_stop.set()
        choose_sample(samples, paths.screenshot)
        loaded = f'{executable}" at' in paths.log.read_text(encoding="utf-8", errors="replace")
        print(
            f"RUN: {name} game_image_loaded={'yes' if loaded else 'no'} "
            f"wrapper_exit={wrapper_exit} log={paths.log}"
        )
    finally:
        key_stop.set()
        if wine is not None and wine.poll() is None:
            stop_process(wine)
        stop_process(xvfb)


def parse_args(argv: Sequence[str] | None = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("name")
    parser.add_argument("seconds", nargs="?", type=float, default=40.0)
    return parser.parse_args(argv)


def main(argv: Sequence[str] | None = None) -> int:
    args = parse_args(argv)
    root = Path(__file__).resolve().parents[1]
    try:
        run(root, args.name, args.seconds, os.environ)
    except (BuildRefusal, ValueError) as exc:
        print(f"run_shim: {exc}", file=sys.stderr)
        return 2
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
