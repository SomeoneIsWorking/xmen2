"""Live cases for the render resolution (src/presentation/render_size.h).

`native-resolution` boots to the main menu five times:

- as a phone whose one window is its 2728x1264 display (`display.emulate`),
  with the default Native choice and then with 720p;
- borderless on this desktop at Native, from a fresh profile and from one
  holding a stale 1600x900, where the device must be the display's measured
  size (GetDeviceCaps' "real desktop mode" line, the same SDL measurement a
  shown window gets);
- in a desktop window, where Native is the window's stored size.

Each run must create its D3D8 device at the resolved size with no retail
"Display failed!" revert, and the headless capture is that backbuffer. The
borderless runs open Port Settings on its Video tab and keep that shot, whose
Resolution row names the size rendered.
"""

from __future__ import annotations

import re
import struct
import time
from pathlib import Path

from live_harness import Case

PHONE = (2728, 1264)
# The Video tab in Port Settings' 1280x720 layout, scaled to the window.
VIDEO_TAB = (279 / 1280, 97 / 720)


def png_size(path: Path) -> tuple[int, int]:
    data = path.read_bytes()[:24]
    if data[:8] != b"\x89PNG\r\n\x1a\n":
        return (0, 0)
    return struct.unpack(">II", data[16:24])


def logged_size(case: Case, pattern: str) -> tuple[int, int]:
    found = re.search(pattern, case.log_text())
    return (int(found.group(1)), int(found.group(2))) if found else (0, 0)


def device_size(case: Case) -> tuple[int, int]:
    return logged_size(case, r"d3d8: CreateDevice .*? (\d+)x(\d+) fmt")


def boot(case: Case, run: str, conf: list[str], emulate: str = "",
         settings: bool = False) -> None:
    case.shutdown()
    case.prepare_profile(["boot.mode=normal", *conf])
    if emulate:
        case.runtime_settings.append("display.emulate=" + emulate)
    env = {"X2_FILES": "1"}
    if settings:
        env["X2_SETTINGS_OPEN"] = "1"
    case.launch(env)
    case.wait_control(60)
    case.check("%s: main menu opened" % run,
               case.wait_log("menus/main.pkgb", 300))
    time.sleep(3.0)


def judge(case: Case, run: str, expected: tuple[int, int]) -> None:
    shot = case.shot(run)
    (case.dir.parent / ("%s.png" % run)).write_bytes(shot.read_bytes())
    case.check("%s: D3D8 device created at %dx%d" % (run, *expected),
               device_size(case) == expected, str(device_size(case)))
    case.check("%s: backbuffer capture is %dx%d" % (run, *expected),
               png_size(shot) == expected, str(png_size(shot)))
    case.check("%s: retail accepted the mode" % run,
               "Display failed" not in case.log_text())


def video_tab(case: Case, run: str, size: tuple[int, int]) -> None:
    case.http("/ui/click?x=%d&y=%d" % (VIDEO_TAB[0] * size[0],
                                      VIDEO_TAB[1] * size[1]))
    time.sleep(1.5)
    shot = case.shot(run + "-video")
    (case.dir.parent / ("%s-video.png" % run)).write_bytes(shot.read_bytes())


def borderless(case: Case, run: str, conf: list[str]) -> None:
    boot(case, run, ["video.mode=borderless", *conf], settings=True)
    display = logged_size(case, r"GetDeviceCaps answering from SDL's real "
                                r"desktop mode: (\d+)x(\d+)")
    case.check("%s: the display was measured, not assumed" % run,
               display != (0, 0), str(display))
    case.check("%s: the seed published Native at the display size" % run,
               "video %dx%d (Native)" % display in case.log_text())
    judge(case, run, display)
    video_tab(case, run, display)


def case_native_resolution(case: Case) -> None:
    boot(case, "phone-native", ["video.mode=windowed"], "%dx%d" % PHONE)
    case.check("phone-native: the seed published Native",
               "video 2728x1264 (Native)" in case.log_text())
    judge(case, "phone-native", PHONE)

    boot(case, "phone-720", ["video.mode=windowed", "video.resolution=720"],
         "%dx%d" % PHONE)
    judge(case, "phone-720", (1554, 720))

    borderless(case, "borderless-fresh", [])
    borderless(case, "borderless-stale", ["video.width=1600",
                                          "video.height=900"])

    boot(case, "desktop-windowed", ["video.mode=windowed",
                                    "video.width=1280", "video.height=720"])
    judge(case, "desktop-windowed", (1280, 720))
