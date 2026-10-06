"""Visible-window product cases driven through the real SDL event path."""

from __future__ import annotations

import os
import shutil
import subprocess
import time
from pathlib import Path


def _require_x11_tools() -> None:
    missing = [tool for tool in ("xdotool",) if not shutil.which(tool)]
    if missing:
        raise RuntimeError("missing required visible-window tool(s): %s"
                           % ", ".join(missing))
    if not os.environ.get("DISPLAY"):
        raise RuntimeError("DISPLAY is unset; run this case under xvfb-run")


def _xdotool(*args: str) -> str:
    result = subprocess.run(
        ["xdotool", *args], check=True, text=True,
        stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    return result.stdout.strip()


def _wait_window(case, timeout: float = 60.0) -> str:
    assert case.proc
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        result = subprocess.run(
            ["xdotool", "search", "--onlyvisible", "--pid",
             str(case.proc.pid)], text=True, stdout=subprocess.PIPE,
            stderr=subprocess.DEVNULL)
        windows = result.stdout.split()
        if windows:
            return windows[-1]
        if not case.alive():
            break
        time.sleep(0.25)
    raise RuntimeError("the visible game window did not appear")


def _geometry(window: str) -> tuple[int, int]:
    values = {}
    for line in _xdotool("getwindowgeometry", "--shell", window).splitlines():
        if "=" in line:
            key, value = line.split("=", 1)
            values[key] = value
    return int(values["WIDTH"]), int(values["HEIGHT"])


def _click(window: str, x: int, y: int) -> None:
    _xdotool("windowfocus", "--sync", window)
    _xdotool("mousemove", "--sync", "--window", window, str(x), str(y))
    _xdotool("click", "1")


def _png_size(path: Path) -> tuple[int, int]:
    from PIL import Image
    with Image.open(path) as image:
        return image.size


def _menu_geometry_overlap(first: Path, second: Path) -> float:
    """Compare the static orange menu geometry, ignoring the moving scene."""
    from PIL import Image

    def mask(path: Path) -> set[tuple[int, int]]:
        with Image.open(path) as source:
            image = source.convert("RGB")
        result = set()
        # The 1280x720 retail main-menu rows occupy this fixed region in the
        # game's native widescreen layout. The animated background is excluded
        # by both the crop and the authored orange/bronze chroma test.
        for y in range(220, 630):
            for x in range(150, 520):
                red, green, blue = image.getpixel((x, y))
                if (red > 120 and red > green * 1.35
                        and green > blue * 1.03):
                    result.add((x, y))
        return result

    first_mask, second_mask = mask(first), mask(second)
    union = first_mask | second_mask
    return len(first_mask & second_mask) / len(union) if union else 0.0


def _png_mean_diff(first: Path, second: Path) -> float:
    from PIL import Image

    with Image.open(first) as source:
        first_pixels = list(source.convert("L").getdata())
    with Image.open(second) as source:
        second_pixels = list(source.convert("L").getdata())
    if len(first_pixels) != len(second_pixels):
        return 0.0
    step = max(1, len(first_pixels) // 4096)
    samples = range(0, len(first_pixels), step)
    return sum(abs(first_pixels[index] - second_pixels[index])
               for index in samples) / len(samples)


def case_live_resolution(case) -> None:
    """Change resolution in Port Settings and prove the live target changes."""
    _require_x11_tools()
    case.prepare_profile([
        "boot.mode=normal",
        "video.width=800",
        "video.height=600",
        "video.mode=windowed",
    ])
    case.launch({
        "SDL_VIDEODRIVER": "x11",
        "X2_FILES": "1",
        "X2_SETTINGS_OPEN": "1",
    }, visible=True)
    case.wait_control(60)
    case.check("main-menu lifecycle opened",
               case.wait_log("menus/main.pkgb", 300))
    window = _wait_window(case)
    case.check("visible window starts at 800x600",
               _geometry(window) == (800, 600), str(_geometry(window)))
    before = case.shot("before")
    case.check("active render starts at 800x600",
               _png_size(before) == (800, 600), str(_png_size(before)))

    # 800x600 overlay: Video is the second top tab; Resolution is the first
    # control in its left pane. These coordinates target the authored RmlUi
    # document, not the retail game's coordinate mapping under test elsewhere.
    _click(window, 205, 48)
    time.sleep(0.5)
    case.shot("video-tab")
    _click(window, 220, 160)
    time.sleep(2.0)

    after = case.shot("after")
    window_size = _geometry(window)
    render_size = _png_size(after)
    case.check("Port Settings resizes the existing window immediately",
               window_size == (1280, 720), str(window_size))
    case.check("the active render target changes without restarting",
               render_size == (1280, 720), str(render_size))
    settings = (case.profile / "x2native.conf").read_text(errors="replace")
    case.check("the live resolution is persisted",
               "video.width=1280" in settings and "video.height=720" in settings)

    _click(window, 1150, 96)
    time.sleep(2.0)
    live_menu = case.shot("live-main-menu")
    case.shutdown()
    case.log_path = case.dir / "cold-1280.log"
    case.launch({"SDL_VIDEODRIVER": "x11", "X2_FILES": "1"}, visible=True)
    case.wait_control(60)
    case.check("cold 1280x720 main-menu lifecycle opened",
               case.wait_log("menus/main.pkgb", 300))
    time.sleep(2.0)
    cold_menu = case.shot("cold-main-menu")
    overlap = _menu_geometry_overlap(live_menu, cold_menu)
    case.check("live resize uses the native widescreen menu geometry",
               overlap > 0.75, "orange-menu overlap %.3f" % overlap)


def case_mouse_click(case) -> None:
    """Click NEW GAME through X11/SDL and prove the retail dialog responds."""
    _require_x11_tools()
    case.prepare_profile([
        "boot.mode=normal",
        "video.width=800",
        "video.height=600",
        "video.mode=windowed",
    ])
    case.launch({"SDL_VIDEODRIVER": "x11", "X2_FILES": "1"}, visible=True)
    case.wait_control(60)
    case.check("main-menu map lifecycle opened",
               case.wait_log("menus/main.pkgb", 300))
    window = _wait_window(case)
    before = case.shot("main-menu")
    time.sleep(1.0)

    # NEW GAME is the first retail menu row at 800x600. The click enters the
    # process as an X11 event, then travels through SDL and the Win32 queue to
    # the retained game WndProc.
    _click(window, 160, 215)
    time.sleep(3.0)
    after = case.shot("after-click")
    case.check("the click changes the retail frame",
               _png_mean_diff(before, after) > 2.0)
    case.check("the NEW GAME click opens the difficulty dialog",
               "Choose a difficulty level:" in case.log_text())
