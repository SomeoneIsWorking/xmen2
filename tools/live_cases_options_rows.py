"""Live case for the port's rows in the retail Options screen (plan #203).

`options-rows` opens the game's Options from the main menu twice:

- on this desktop in a 1600x900 window: the port's rows are in the game's
  own menu after Vibration, Advanced Options is gone, the keyboard walks
  into them, Return on Resolution renders 720p and Right 1080p at once, a
  mouse click on Dynamic Shadows turns them off, and each change is in
  x2native.conf;
- as a phone whose one window is its 2728x1264 display (`display.emulate`)
  with touch controls: there is no Window Mode row, a tap on Resolution
  renders 720p (1554x720), and a relaunch on the same profile starts at it.

Each change is judged by what the game renders (the backbuffer capture's
size), the setting file, and the row's value as GET /menu reads it.
"""

from __future__ import annotations

import time

from live_cases_resolution import PHONE, device_size, png_size
from live_harness import Case
from live_menu import open_options, read_menu, wait_menu

PORT_ROWS = ["resolution", "window mode", "dynamic shadows", "shadow quality",
             "hud layout", "health size", "potion size", "portrait size",
             "edge spacing", "touch controls", "extraction revive", "boot"]


def labels(menu: dict) -> list[str]:
    return [row["label"].lower() for row in menu.get("rows", [])]


def row(menu: dict, label: str) -> dict:
    return next((r for r in menu.get("rows", [])
                 if r["label"].lower() == label), {})


def focused(menu: dict) -> str:
    rows = menu.get("rows", [])
    at = menu.get("focused_row", -1)
    return rows[at]["label"].lower() if 0 <= at < len(rows) else ""


def setting(case: Case, key: str) -> str:
    text = (case.profile / "x2native.conf").read_text(errors="replace")
    for line in text.splitlines():
        if line.startswith(key + "="):
            return line.split("=", 1)[1]
    return ""


def change(case: Case, request: str, text: str, key: str, value: str,
           timeout: float = 20.0) -> bool:
    """GET `request`, then a new `text` line in the log and `key` saved as
    `value`."""
    before = case.log_text().count(text)
    case.http(request)
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        if case.log_text().count(text) > before and setting(case, key) == value:
            return True
        time.sleep(0.5)
    return False


def shot_size(case: Case, name: str) -> tuple[int, int]:
    shot = case.shot(name)
    (case.dir.parent / ("%s.png" % name)).write_bytes(shot.read_bytes())
    return png_size(shot)


def boot_to_options(case: Case, log: str, conf: list[str] | None,
                    emulate: str = "") -> dict:
    case.shutdown()
    case.log_path = case.dir / ("%s.log" % log)
    if conf is not None:
        case.prepare_profile(["boot.mode=menu", *conf])
    case.runtime_settings = [s for s in case.runtime_settings
                             if not s.startswith("display.emulate=")]
    if emulate:
        case.runtime_settings.append("display.emulate=" + emulate)
    case.launch({"X2_FILES": "1"})
    case.wait_control(60)
    case.wait_log("menus/main.pkgb", 300)
    wait_menu(case, "main", 60)
    open_options(case)
    time.sleep(2.0)
    return read_menu(case)


def key_to(case: Case, label: str) -> bool:
    for _ in range(30):
        if focused(read_menu(case)) == label:
            return True
        case.http("/key?name=Down&hold=0.15")
        time.sleep(0.6)
    return focused(read_menu(case)) == label


def desktop(case: Case) -> None:
    menu = boot_to_options(case, "desktop", ["video.mode=windowed",
                                             "video.width=1600",
                                             "video.height=900"])
    names = labels(menu)
    case.check("desktop: the derived Options menu was served",
               'REPLACED "ui/menus/options.' in case.log_text())
    case.check("desktop: every port row is in the game's Options",
               all(r in names for r in PORT_ROWS), str(names))
    case.check("desktop: the port rows follow Vibration, before Accept",
               names.index("resolution") == names.index("vibration") + 1
               and names[-1] == "accept", str(names))
    case.check("desktop: Advanced Options is gone from the footer",
               not any("advanced" in d.lower() for d in menu.get("desctext", [])),
               str(menu.get("desctext")))
    case.check("desktop: starts rendering its 1600x900 window",
               shot_size(case, "desktop-before") == (1600, 900))
    case.check("desktop: the keyboard walks into the port rows",
               key_to(case, "resolution"), focused(read_menu(case)))
    shot_size(case, "desktop-options")
    to_720 = "PORT OPTIONS: Resolution is now 720p (1280x720)"
    case.check("desktop: Return on Resolution renders 720p and saves it",
               change(case, "/key?name=Return&hold=0.15", to_720, "video.resolution", "720")
               and shot_size(case, "desktop-720") == (1280, 720),
               setting(case, "video.resolution"))
    case.check("desktop: Right on Resolution renders 1080p and saves it",
               change(case, "/key?name=Right&hold=0.15", "PORT OPTIONS: Resolution is now 1080p "
                           "(1920x1080)", "video.resolution", "1080")
               and shot_size(case, "desktop-1080") == (1920, 1080),
               setting(case, "video.resolution"))
    case.check("desktop: Left steps back to 720p",
               change(case, "/key?name=Left&hold=0.15", to_720, "video.resolution", "720"),
               setting(case, "video.resolution"))
    time.sleep(1.5)
    menu = read_menu(case)
    shadows = row(menu, "dynamic shadows").get("tap")
    case.check("desktop: Dynamic Shadows has a place on screen", bool(shadows))
    if shadows:
        width, height = 1280, 720
        click = "/ui/click?x=%d&y=%d" % (shadows["x"] * width,
                                         shadows["y"] * height)
        case.check("desktop: a mouse click on Dynamic Shadows turns them off",
                   change(case, click, "PORT OPTIONS: Dynamic Shadows is now Off",
                               "video.dynamic_shadows", "0"),
                   setting(case, "video.dynamic_shadows"))
    shot_size(case, "desktop-after")


def phone(case: Case) -> None:
    emulate = "%dx%d" % PHONE
    menu = boot_to_options(case, "phone", ["video.mode=windowed",
                                           "input.touch_controls=2"], emulate)
    names = labels(menu)
    case.check("phone: there is no Window Mode row",
               "window mode" not in names and "resolution" in names, str(names))
    case.check("phone: starts at the display's size",
               shot_size(case, "phone-before") == PHONE)
    resolution = row(menu, "resolution").get("tap")
    case.check("phone: Resolution has a place on screen", bool(resolution))
    tap = ("/touch?x=%g&y=%g" % (resolution["x"], resolution["y"])
           if resolution else "/status")
    case.check("phone: a tap on Resolution renders 720p and saves it",
               change(case, tap, "PORT OPTIONS: Resolution is now 720p "
                           "(1554x720)", "video.resolution", "720")
               and shot_size(case, "phone-720") == (1554, 720),
               setting(case, "video.resolution"))
    case.check("phone: the window was left alone",
               "setting the windowed resolution failed" not in case.log_text()
               and "rollback failed" not in case.log_text())
    boot_to_options(case, "phone-relaunch", None, emulate)
    case.check("phone: a relaunch renders the saved 720p",
               device_size(case) == (1554, 720), str(device_size(case)))
    shot_size(case, "phone-relaunch")


def case_options_rows(case: Case) -> None:
    desktop(case)
    phone(case)
