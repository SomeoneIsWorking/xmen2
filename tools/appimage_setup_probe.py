#!/usr/bin/env python3
"""Exercise the packaged no-terminal setup path through its SDL dialog boundary."""

from __future__ import annotations

import argparse
import json
import os
from pathlib import Path
import select
import shutil
import subprocess
import tempfile
import time


ROOT = Path(__file__).resolve().parents[1]
SCRATCH = ROOT / "scratch/run"
DIALOG_STUB = ROOT / "tests/appimage_dialog_stub.py"


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--appimage", type=Path, required=True)
    parser.add_argument("--game-dir", type=Path, required=True)
    return parser.parse_args()


def stop(process: subprocess.Popen[str]) -> None:
    if process.poll() is not None:
        return
    process.terminate()
    try:
        process.wait(timeout=3)
    except subprocess.TimeoutExpired:
        process.kill()
        process.wait(timeout=3)


def start_xvfb(output) -> tuple[subprocess.Popen[str], str]:
    executable = shutil.which("Xvfb")
    if not executable:
        raise SystemExit("appimage setup: Xvfb is required for the dialog probe")
    read_fd, write_fd = os.pipe()
    try:
        process = subprocess.Popen(
            [executable, "-displayfd", str(write_fd), "-screen", "0", "640x480x24",
             "-nolisten", "tcp"],
            pass_fds=(write_fd,),
            text=True,
            stdout=output,
            stderr=subprocess.STDOUT,
        )
    finally:
        os.close(write_fd)
    ready, _, _ = select.select([read_fd], [], [], 5)
    if not ready:
        os.close(read_fd)
        stop(process)
        raise SystemExit("appimage setup: Xvfb did not publish a display")
    with os.fdopen(read_fd, encoding="ascii") as display_output:
        display = display_output.readline().strip()
    if not display:
        stop(process)
        raise SystemExit("appimage setup: Xvfb returned an empty display")
    return process, f":{display}"


def require_dialog(invocations: list[list[str]], option: str) -> list[str]:
    matches = [arguments for arguments in invocations if option in arguments]
    if len(matches) != 1:
        raise SystemExit(
            f"appimage setup: expected one Zenity {option} call, found {len(matches)}"
        )
    return matches[0]


def main() -> int:
    args = parse_args()
    appimage = args.appimage.resolve()
    game = args.game_dir.resolve()
    executable = game / "XMen2.exe"
    if not appimage.is_file() or not os.access(appimage, os.X_OK):
        raise SystemExit(f"appimage setup: artifact is not executable: {appimage}")
    if not executable.is_file():
        raise SystemExit(f"appimage setup: XMen2.exe is missing: {executable}")

    SCRATCH.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="appimage-setup-probe-", dir=SCRATCH) as raw:
        directory = Path(raw)
        stub_directory = directory / "bin"
        stub_directory.mkdir()
        (stub_directory / "zenity").symlink_to(DIALOG_STUB)
        config = directory / "config"
        dialog_log = directory / "dialogs.jsonl"
        output_log = directory / "appimage.log"
        xvfb_log = directory / "xvfb.log"
        environment = os.environ.copy()
        environment.pop("GAME_PC_DIR", None)
        environment.pop("WAYLAND_DISPLAY", None)
        environment.update({
            "DISABLE_LSFG": "1",
            "PATH": f"{stub_directory}{os.pathsep}{environment['PATH']}",
            "SDL_FILE_DIALOG_DRIVER": "zenity",
            "SDL_VIDEODRIVER": "x11",
            "X2_DIALOG_LOG": str(dialog_log),
            "X2_DIALOG_SELECTION": str(executable),
            "XDG_CONFIG_HOME": str(config),
        })
        with (output_log.open("w", encoding="utf-8") as output,
              xvfb_log.open("w", encoding="utf-8") as xvfb_output):
            xserver, environment["DISPLAY"] = start_xvfb(xvfb_output)
            try:
                process = subprocess.Popen(
                    [str(appimage)],
                    cwd=ROOT,
                    env=environment,
                    text=True,
                    stdout=output,
                    stderr=subprocess.STDOUT,
                )
                try:
                    preference = config / "xmen2/install-path.txt"
                    deadline = time.monotonic() + 30
                    while time.monotonic() < deadline and not preference.is_file():
                        if process.poll() is not None:
                            break
                        time.sleep(0.05)
                finally:
                    stop(process)
            finally:
                stop(xserver)

        output = output_log.read_text(encoding="utf-8")
        if not preference.is_file():
            raise SystemExit(
                "appimage setup: selection was not persisted through the packaged path\n"
                + output[-4000:]
            )
        if preference.read_text(encoding="utf-8").strip() != str(game):
            raise SystemExit("appimage setup: persisted install directory disagrees")
        if "loaded " in output and ".env" in output:
            raise SystemExit("appimage setup: packaged launch loaded a developer .env")
        invocations = [json.loads(line) for line in dialog_log.read_text(
            encoding="utf-8").splitlines()]
        prompt = require_dialog(invocations, "--question")
        picker = require_dialog(invocations, "--file-selection")
        expected_prompt = (
            "--title", "X-Men Legends II installation",
            "--extra-button", "Browse",
            "--extra-button", "Quit",
        )
        for option, value in zip(expected_prompt[::2], expected_prompt[1::2],
                                 strict=True):
            if option not in prompt or value not in prompt:
                raise SystemExit(f"appimage setup: prompt omitted {option} {value!r}")
        if not any("X-Men Legends II executable" in value for value in picker):
            raise SystemExit("appimage setup: picker omitted the executable filter")
    print("appimage setup: prompt, Browse, selection, and OS-config persistence passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
