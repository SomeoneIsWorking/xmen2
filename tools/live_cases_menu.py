"""Live cases for the retail menus and dialogs."""

from __future__ import annotations

import json
import re
import shutil
import signal
import time

from live_game import (
    live_controls,
    live_viewport,
    tap_control,
)
from live_harness import Case
from live_menu import (
    MAIN_MENU_LABELS,
    OPTIONS_MENU_LABELS,
    MenuTimeline,
    keyboard_past_the_intro,
    open_options,
    read_menu,
    row_labels,
    tap_touch_button,
    touch_button,
    touch_menu,
    wait_menu,
    wait_movie_end,
    wait_touch_menu,
    walk_rows_by_key,
)

# Top of NEW GAME's default selected difficulty row as a share of output
# height, measured at 800x600 where the title's own layout runs uncorrected.
SELECTED_ROW_RETAIL_TOP_SHARE = 0.2996


# A row pitch is ~0.021 of the output; this admits rounding, not a row.
SELECTED_ROW_TOP_TOLERANCE = 0.005


def case_selector_dialog(case: Case) -> None:
    """Reach New Game's difficulty dialog and record the untextured
    eight-primitive draw class containing its selected-row geometry."""
    if case.name.endswith("4k"):
        width, height = 3840, 2160
    elif case.name.endswith("720"):
        width, height = 1280, 720
    else:
        width, height = 800, 600
    evidence = case.dir / "selector-untextured-8.jsonl"
    case.prepare_profile([
        "boot.mode=normal",
        "video.width=%d" % width,
        "video.height=%d" % height,
        "video.mode=windowed",
    ])
    case.launch({
        "X2_FILES": "1",
        "X2_SELECTOR_PROBE": str(evidence),
        "X2_SELECTOR_TEXTURE": "untextured:8",
    })
    case.wait_control(60)
    case.check("main-menu map lifecycle opened",
               case.wait_log("menus/main.pkgb", 300))
    time.sleep(5)
    case.shot("main-menu")
    code, body = case.http("/key?name=Return&hold=0.4")
    case.check("Return accepted on NEW GAME", code == 200,
               body.decode(errors="replace").strip())
    time.sleep(5)
    dialog = case.shot("difficulty-dialog")
    case.check("difficulty capture is a PNG",
               dialog.read_bytes().startswith(b"\x89PNG\r\n\x1a\n"))
    case.signal(signal.SIGUSR1)
    case.check("the dialog has a complete frame draw table",
               case.wait_log("[FRAME TABLE] end of frame", 30))

    from selector_probe import parse_records, summarize
    try:
        summary = summarize(parse_records(evidence))
    except Exception as exc:
        case.check("selector evidence is parseable", False, str(exc))
        return
    row_candidates = [
        item for item in summary.candidates
        if item["fvf"] == "00000042"
        and item["primitive_count"] == 8
        and item["bounds_valid"]
        and item["max_x"] - item["min_x"] > width * 0.4
    ]
    case.check("untextured eight-primitive requests reached the v13 probe",
               bool(summary.candidates), "%d request(s)" % len(summary.candidates))
    case.check("every recorded build request has a result",
               len(summary.results) == len(summary.candidates))
    case.check("the selected-row geometry class was observed",
               bool(row_candidates), "%d candidate(s)" % len(row_candidates))
    row_heights = [item["max_y"] - item["min_y"]
                   for item in row_candidates]
    case.check("the selected row keeps its retail-relative height",
               bool(row_heights) and max(row_heights) >= height * 0.025,
               "max %.2f px of %d" % (max(row_heights, default=0.0), height))
    # The title derives the row's translation from the same scale #133
    # corrects; correcting only the scale left a row of the right height one
    # full row below its text at 2160 lines (#185). 800x600 applies no
    # correction, so its measured top IS the retail placement, and this same
    # check pins it there.
    row_tops = [item["min_y"] / height for item in row_candidates]
    case.check("the selected row sits where the retail 800x600 layout puts it",
               bool(row_tops) and all(
                   abs(top - SELECTED_ROW_RETAIL_TOP_SHARE)
                   < SELECTED_ROW_TOP_TOLERANCE for top in row_tops),
               "tops %s of %d, retail %.4f" % (
                   sorted({round(top, 4) for top in row_tops}), height,
                   SELECTED_ROW_RETAIL_TOP_SHARE))

    expected_mode = "%dx%d" % (width, height)
    cold_log = case.log_text()
    case.check("cold profile creates the configured D3D device",
               "CreateDevice adapter=0 hardware-vertex %s" % expected_mode
               in cold_log)
    case.check("cold profile resolves through the retail settings reader",
               "DISPLAY RUNTIME: retail settings resolved configured mode %s"
               % expected_mode in cold_log)
    registry = (case.profile / "registry.txt").read_text(errors="replace")
    case.check("cold profile persists the configured retail Resolution",
               expected_mode.encode().hex() in registry)

    # The fresh-profile branch and Version=7 warm branch are different retail
    # code paths. Preserve the cold evidence, then boot the SAME profile again
    # so one passing branch cannot be presented as evidence for the other.
    case.shutdown()
    shutil.copy2(case.log_path, case.dir / "cold-run.log")
    warm_evidence = case.dir / "selector-untextured-8-warm.jsonl"
    case.log_path = case.dir / "warm-run.log"
    case.launch({
        "X2_FILES": "1",
        "X2_SELECTOR_PROBE": str(warm_evidence),
        "X2_SELECTOR_TEXTURE": "untextured:8",
    })
    case.wait_control(60)
    case.check("warm-profile main-menu lifecycle opened",
               case.wait_log("menus/main.pkgb", 300))
    warm_log = case.log_text()
    case.check("warm profile keeps the configured seed",
               "DISPLAY SEED: no change needed for video %s" % expected_mode
               in warm_log)
    case.check("warm profile creates the configured D3D device",
               "CreateDevice adapter=0 hardware-vertex %s" % expected_mode
               in warm_log)
    warm_shot = case.shot("warm-main-menu")
    try:
        from PIL import Image
        with Image.open(warm_shot) as image:
            warm_size = image.size
    except Exception:
        warm_size = (0, 0)
    case.check("warm capture has the configured dimensions",
               warm_size == (width, height), "%dx%d" % warm_size)


INTRO_TAP_SECONDS = 6.0


def case_menu_touch(case: Case) -> None:
    """Issue #179: a finger on the screens BEFORE gameplay.

    The overlay is deliberately not drawn during the intro and the menus, and
    every contact there used to be counted and discarded -- so a phone player
    met an intro no tap could skip and a menu no tap could press. Those
    screens are the retail GUI, which takes a mouse, so a contact with no
    drawn control under it is now that pointer.

    Both halves are checked against a control that must come out the other
    way: an untouched movie runs its full length where a tapped one ends when
    the tap happened, and a tap on empty sky opens nothing where a tap on a
    menu row opens the package that row names.
    """
    case.prepare_profile(["boot.mode=normal", "input.touch_controls=2"])
    case.launch({"X2_FILES": "1"})
    case.wait_control(60)

    # -- the intro, timed from the movie's own load line.
    case.check("an intro movie loaded",
               case.wait_log("movie: loaded native", 180))
    ended = len(re.findall(r"native FMV: \d+ video decoded", case.log_text()))
    loaded_at = time.monotonic()
    time.sleep(INTRO_TAP_SECONDS)
    case.check("the tap was routed", case.http("/touch?x=0.5&y=0.5")[0] == 200)
    tapped = wait_movie_end(case, ended, 90)
    case.check("the tapped movie ended at all", tapped is not None)
    if tapped is None:
        return
    elapsed = tapped - loaded_at
    case.check("a tap ended the intro movie when the tap happened, not when "
               "the movie would have",
               elapsed < INTRO_TAP_SECONDS + 3.0,
               "%.1fs after it loaded, tapped at %.1fs; untouched this movie "
               "runs 10.0s" % (elapsed, INTRO_TAP_SECONDS))

    # -- the main menu.
    case.check("the run reached the main menu",
               keyboard_past_the_intro(case, 300))
    case.shot("menu")

    # The main menu is the touch menu's: its rows are covered by the
    # touch-menu case. Here only the census's account of this run is checked.
    shown = wait_touch_menu(case, "main", 30)
    case.check("the touch menu covers the main menu", shown.get("visible") is True)
    beats = len(re.findall(r"went to the touch menu", case.log_text()))
    case.http("/touch?x=0.02&y=0.5")
    deadline = time.monotonic() + 30
    while time.monotonic() < deadline:
        if len(re.findall(r"went to the touch menu", case.log_text())) > beats:
            break
        time.sleep(1.0)
    report = case.log_text()
    pointers = [int(n) for n in re.findall(
        r"(\d+) contact\(s\) became the retail GUI pointer", report)]
    menu_contacts = [int(n) for n in re.findall(
        r"(\d+) contact event\(s\) went to the touch menu", report)]
    dropped = [int(n) for n in re.findall(
        r"(\d+) of \d+ contact\(s\) were dropped before routing", report)]
    case.check("the census counted the intro tap as the retail pointer",
               bool(pointers) and max(pointers) >= 2,
               "%d at most" % max(pointers, default=0))
    case.check("and the menu tap as the touch menu's",
               bool(menu_contacts) and max(menu_contacts) >= 2,
               "%d at most" % max(menu_contacts, default=0))
    case.check("and reported none of them dropped",
               bool(dropped) and max(dropped) == 0,
               "%d at most" % max(dropped, default=-1))


def case_menu_pad(case: Case) -> None:
    """The menu pad where the touch menu stands aside.

    New Game raises the difficulty popup over the main menu; the touch menu
    hides under a popup, touch play draws the d-pad and face buttons, and the
    pad's B must close the popup.
    """
    case.prepare_profile(["boot.mode=normal", "input.touch_controls=2"])
    case.launch({"X2_FILES": "1"})
    case.wait_control(60)
    case.check("the run reached the main menu",
               keyboard_past_the_intro(case, 300))
    main = wait_touch_menu(case, "main", 30)
    new_game = touch_button(main, "row", "new game")
    case.check("the touch menu offers NEW GAME", new_game is not None)
    if new_game is None:
        return
    timeline = MenuTimeline(case, "timeline.txt")
    tap_touch_button(case, main, new_game)
    menu = timeline.until(lambda m: m.get("popup") is True, 20)
    case.check("NEW GAME opened the game's difficulty popup",
               menu.get("menu") == "main" and menu.get("popup") is True,
               "menu %r popup %r" % (menu.get("menu"), menu.get("popup")))
    timeline.until(lambda m: not m.get("touch_menu", {}).get("visible"), 5)
    controls = live_controls(case)
    viewport = live_viewport(case)
    case.check("the touch menu stands aside there",
               touch_menu(case).get("visible") is False)
    case.check("and the menu pad is drawn instead",
               viewport is not None and "menu-b" in controls,
               "drawn: %s" % sorted(controls))
    case.check("and none of the gameplay controls", "jump" not in controls)
    case.shot("new-game")
    if viewport is None or "menu-b" not in controls:
        return
    case.check("B was tapped", tap_control(case, controls["menu-b"], viewport))
    deadline = time.monotonic() + 20
    back = read_menu(case)
    while time.monotonic() < deadline and back.get("popup") is not False:
        time.sleep(0.5)
        back = read_menu(case)
    case.check("the pad's B closed the popup on the game's main menu",
               back.get("menu") == "main" and back.get("popup") is False,
               "menu %r popup %r" % (back.get("menu"), back.get("popup")))
    case.shot("after-back")


def case_menu_model(case: Case) -> None:
    """GET /menu: the active retail menu read from the guest.

    The labels are the game's localized text as drawn, and the row order is
    checked against the game itself: one Down per row must move the game's own
    focus to the next row the model listed, wrapping at the end.
    """
    case.prepare_profile(["boot.mode=normal"])
    case.launch({"X2_FILES": "1"})
    case.wait_control(60)
    case.check("the run reached the main menu",
               keyboard_past_the_intro(case, 300))
    main = wait_menu(case, "main", 30)
    (case.dir / "main.json").write_text(json.dumps(main, indent=1) + "\n")
    print("  main: %s" % row_labels(main))
    case.check("/menu names the main menu and its class",
               main.get("menu") == "main" and main.get("class") == "CMenuMain",
               "%s %s" % (main.get("menu"), main.get("class")))
    case.check("the main menu's rows are its English labels in order",
               row_labels(main) == MAIN_MENU_LABELS, str(row_labels(main)))
    start = main.get("focused_row", -1)
    count = len(main.get("rows", []))
    walked = walk_rows_by_key(case, main)
    expected = [(start + i + 1) % count for i in range(count)] if count else []
    case.check("Down visits the main rows in the listed order",
               start >= 0 and bool(walked) and walked == expected,
               "focus went %s, listed order predicts %s" % (walked, expected))

    if "options" not in row_labels(read_menu(case)):
        return
    options = open_options(case)
    (case.dir / "options.json").write_text(json.dumps(options, indent=1) + "\n")
    print("  options: %s" % row_labels(options))
    case.check("the Options rows are its English labels in order",
               row_labels(options) == OPTIONS_MENU_LABELS,
               str(row_labels(options)))
    by_label = {row["label"].lower(): row for row in options.get("rows", [])}
    volume = by_label.get("effects volume", {})
    case.check("a volume row carries left/right commands and its bar's level",
               volume.get("has_left_right") is True
               and volume.get("fill") is not None,
               "fill %s" % volume.get("fill"))
    combat = by_label.get("combat music", {})
    case.check("a toggle row carries the value it shows",
               combat.get("value") in ("On", "Off"),
               "value %r" % combat.get("value"))
    start = options.get("focused_row", -1)
    count = len(options.get("rows", []))
    walked = walk_rows_by_key(case, options)
    expected = [(start + i + 1) % count for i in range(count)] if count else []
    case.check("Down visits the Options rows in the listed order",
               start >= 0 and bool(walked) and walked == expected,
               "focus went %s, listed order predicts %s" % (walked, expected))
    case.shot("options")


def case_options_back(case: Case) -> None:
    """Escape leaves Options for the PC Advanced Options screen (sebas), whose
    text the prompt glyphs must leave alone."""
    case.prepare_profile(["boot.mode=normal"])
    case.launch({"X2_FILES": "1"})
    case.wait_control(60)
    case.check("the run reached the main menu",
               keyboard_past_the_intro(case, 300))
    wait_menu(case, "main", 30)
    case.check("Options opened", open_options(case).get("menu") == "options")
    time.sleep(1.0)
    case.http("/key?name=Escape&hold=0.15")
    advanced = wait_menu(case, "sebas", 20)
    case.check("Escape opened Advanced Options", advanced.get("menu") == "sebas",
               str(advanced.get("menu")))
    time.sleep(2.0)
    case.check("the game survived drawing Advanced Options", case.alive())
    case.shot("advanced")
