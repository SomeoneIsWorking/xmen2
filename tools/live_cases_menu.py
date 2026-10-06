"""Live cases for the retail menus, dialogs and the touch menu."""

from __future__ import annotations

import json
import re
import shutil
import signal
import time

from live_harness import Case
from live_game import live_controls, live_viewport, tap_control
from live_menu import (
    MAIN_MENU_LABELS,
    MenuTimeline,
    OPTIONS_MENU_LABELS,
    keyboard_into_the_pda,
    keyboard_past_the_intro,
    open_options,
    read_menu,
    row_labels,
    shop_list,
    tap_touch_button,
    touch_button,
    touch_menu,
    touch_row,
    wait_game_menu,
    wait_menu,
    wait_movie_end,
    wait_row_change,
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


# The game's own first open of Options: the menu animates, so a pixel delta
# cannot tell whether a row was activated.
OPTIONS_PACKAGE = "menus/options.pkgb"


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


def case_touch_menu(case: Case) -> None:
    """The port's touch menu over the retail main and Options menus.

    Each tap is judged by the game's own state as GET /menu reads it: the menu
    the game has open, a volume bar's level, a toggle's value text. The
    negative comes first, so a run where every tap "works" cannot pass.
    """
    case.prepare_profile(["boot.mode=normal", "input.touch_controls=2"])
    case.launch({"X2_FILES": "1"})
    case.wait_control(60)
    case.check("the run reached the main menu",
               keyboard_past_the_intro(case, 300))
    main = wait_touch_menu(case, "main", 30)
    (case.dir / "main.json").write_text(json.dumps(main, indent=1) + "\n")
    case.check("the touch menu is shown over the main menu",
               main.get("visible") is True and main.get("menu") == "main",
               str({k: main.get(k) for k in ("visible", "menu")}))
    if not main.get("visible"):
        return
    print("  main rows: %s" % [row["label"] for row in main.get("rows", [])])
    case.check("it offers the main menu's rows",
               [row["label"].lower() for row in main["rows"]]
               == MAIN_MENU_LABELS)
    controls = live_controls(case)
    case.check("and the menu pad is not drawn under it",
               "menu-a" not in controls, "drawn: %s" % sorted(controls))
    time.sleep(1.0)
    case.shot("main")

    opened_before = case.log_text().count(OPTIONS_PACKAGE)
    title = {"left": 0.0, "top": 0.0, "width": main["viewport_width"],
             "height": main["viewport_height"] * 0.02}
    tap_touch_button(case, main, title)
    time.sleep(3.0)
    case.check("a tap on no button activates nothing",
               case.log_text().count(OPTIONS_PACKAGE) == opened_before
               and read_menu(case).get("menu") == "main")

    options_button = touch_button(main, "row", "options")
    case.check("the main menu has an OPTIONS button", options_button is not None)
    if options_button is None:
        return
    case.check("OPTIONS was tapped",
               tap_touch_button(case, main, options_button))
    options = wait_touch_menu(case, "options", 20)
    (case.dir / "options.json").write_text(json.dumps(options, indent=1)
                                           + "\n")
    case.check("the tap opened the game's Options menu",
               read_menu(case).get("menu") == "options"
               and options.get("visible") is True,
               "the game's menu is %r" % read_menu(case).get("menu"))
    if not options.get("visible"):
        return
    time.sleep(1.0)
    case.shot("options")

    volume = touch_row(options, "effects volume")
    step = touch_button(options, "step-left", "effects volume")
    case.check("the effects volume row has a step-left button",
               step is not None and volume.get("fill") is not None)
    if step is not None:
        before = volume.get("fill")
        tap_touch_button(case, options, step)
        after = wait_row_change(case, "effects volume", "fill", before, 10)
        case.check("its step-left lowered the game's effects volume",
                   after.get("fill") is not None and before is not None
                   and after["fill"] < before,
                   "%s -> %s" % (before, after.get("fill")))
        options = touch_menu(case)

    toggle = touch_row(options, "combat music")
    toggle_button = touch_button(options, "row", "combat music")
    case.check("the combat music row shows its value",
               toggle.get("value") in ("On", "Off") and toggle_button,
               repr(toggle.get("value")))
    if toggle_button:
        before = toggle.get("value")
        tap_touch_button(case, options, toggle_button)
        after = wait_row_change(case, "combat music", "value", before, 10)
        case.check("tapping it changed the game's value text",
                   after.get("value") in ("On", "Off")
                   and after.get("value") != before,
                   "%r -> %r" % (before, after.get("value")))
        time.sleep(1.0)
        case.shot("options-changed")

    options = touch_menu(case)
    back = touch_button(options, "footer", "back")
    case.check("Options offers the game's Back footer", back is not None,
               str([b["label"] for b in options.get("buttons", [])
                    if b["part"] == "footer"]))
    if back is None:
        return
    tap_touch_button(case, options, back)
    deadline = time.monotonic() + 20
    left = read_menu(case).get("menu")
    while time.monotonic() < deadline and left in (None, "options"):
        time.sleep(0.5)
        left = read_menu(case).get("menu")
    case.check("Back left Options", left not in (None, "options"),
               "the game's menu is now %r" % left)
    time.sleep(2.0)
    case.shot("after-back")
    case.check("and the game is still running",
               case.alive() and case.http("/status")[0] == 200)


def case_touch_team(case: Case) -> None:
    """The touch menu over CMenuTeam's party screen, opened from the PDA.

    Each tap is judged by the game's own state: which hero summary is lit, the
    class's mode, and the menu it has open.
    """
    case.prepare_profile(["boot.mode=continue", "input.touch_controls=2"])
    case.seed_save("autosave.save")
    case.launch({"X2_FILES": "1"})
    case.wait_control(60)
    pda = keyboard_into_the_pda(case, 300)
    case.check("the run reached the PDA", pda.get("menu") == "pda",
               str(pda.get("menu")))
    shown = wait_touch_menu(case, "pda", 20)
    case.check("the touch menu is shown over the PDA",
               shown.get("visible") is True)
    team_button = touch_button(shown, "row", "team management")
    case.check("the PDA offers Team Management", team_button is not None)
    if team_button is None:
        return
    case.shot("pda")
    tap_touch_button(case, shown, team_button)
    team = wait_touch_menu(case, "team", 20)
    (case.dir / "team.json").write_text(json.dumps(team, indent=1) + "\n")
    heroes = [row["label"] for row in team.get("rows", [])]
    print("  party: %s" % heroes)
    case.check("the tap opened the game's team menu on its party",
               read_menu(case).get("menu") == "team"
               and read_menu(case).get("mode") == 0
               and team.get("visible") is True, str(heroes))
    case.check("the touch menu offers one row per hero",
               len(heroes) >= 2 and all(heroes))
    if team.get("visible") is not True or len(heroes) < 2:
        return
    controls = live_controls(case)
    case.check("and the menu pad is not drawn under it",
               "menu-a" not in controls, "drawn: %s" % sorted(controls))
    case.shot("team")

    selected = team.get("focused_row", -1)
    target = 1 if selected != 1 else 0
    row = next(b for b in team["buttons"]
               if b["part"] == "row" and b["index"] == target)
    tap_touch_button(case, team, row)
    picked = wait_game_menu(
        case, lambda m: m.get("touch_menu", {}).get("focused_row") == target,
        10)
    case.check("a tap on %s selected that hero in the game" % heroes[target],
               picked.get("touch_menu", {}).get("focused_row") == target,
               "the lit hero is row %s" %
               picked.get("touch_menu", {}).get("focused_row"))
    case.shot("team-selected")

    tap_touch_button(case, team, row)
    details = wait_game_menu(case, lambda m: m.get("mode", 0) != 0, 10)
    case.check("a second tap opened that hero's details",
               details.get("menu") == "team" and details.get("mode", 0) >= 2,
               "mode %s" % details.get("mode"))
    deadline = time.monotonic() + 5
    controls = live_controls(case)
    while time.monotonic() < deadline and "menu-b" not in controls:
        time.sleep(0.2)
        controls = live_controls(case)
    case.check("where the touch menu stands aside for the menu pad",
               touch_menu(case).get("visible") is False
               and "menu-b" in controls, "drawn: %s" % sorted(controls))
    case.shot("details")
    viewport = live_viewport(case)
    if viewport is None or "menu-b" not in controls:
        return
    tap_control(case, controls["menu-b"], viewport)
    team = wait_game_menu(
        case, lambda m: m.get("mode") == 0
        and m.get("touch_menu", {}).get("visible") is True, 10)
    case.check("the pad's B returned to the party under the touch menu",
               team.get("mode") == 0
               and team.get("touch_menu", {}).get("visible") is True,
               "mode %s" % team.get("mode"))
    shown = touch_menu(case)
    accept = touch_button(shown, "footer", "accept")
    case.check("the party offers the game's Accept footer", accept is not None,
               str([b["label"] for b in shown.get("buttons", [])
                    if b["part"] == "footer"]))
    if accept is None:
        return
    tap_touch_button(case, shown, accept)
    left = wait_game_menu(case, lambda m: m.get("menu") != "team", 10)
    case.check("Accept left the team menu",
               left.get("menu") != "team", "now %r" % left.get("menu"))
    case.shot("after-accept")
    case.check("and the game is still running", case.alive())


def case_touch_shop(case: Case) -> None:
    """The touch menu over CMenuShop, opened by the console from gameplay.

    Each tap is judged by the game's own state: the lit tab, the list box's
    entries and selection, and the menu it has open.
    """
    case.prepare_profile(["boot.mode=continue", "input.touch_controls=2"])
    case.seed_save("autosave.save")
    case.launch({"X2_FILES": "1"})
    case.wait_control(60)
    pda = keyboard_into_the_pda(case, 300)
    case.check("the run reached the PDA", pda.get("menu") == "pda",
               str(pda.get("menu")))
    menu = pda
    deadline = time.monotonic() + 20
    while time.monotonic() < deadline and menu.get("active") is not False:
        case.http("/key?name=Escape&hold=0.2")
        menu = wait_game_menu(case, lambda m: m.get("active") is False, 3)
    case.check("Escape closed the PDA into gameplay",
               menu.get("active") is False, str(menu.get("menu")))
    case.http("/console?command=openmenu%20shop")
    shop = wait_touch_menu(case, "shop", 20)
    (case.dir / "shop.json").write_text(json.dumps(shop, indent=1) + "\n")
    rows = shop.get("rows", [])
    case.check("the touch menu is shown over the game's shop",
               shop.get("visible") is True
               and read_menu(case).get("class") == "CMenuShop",
               str(read_menu(case).get("class")))
    case.check("it offers the three tabs, then the list's entries",
               [r["label"] for r in rows[:3]] == ["buy", "sell", "training"]
               and len(rows) > 3, str([r["label"] for r in rows]))
    if shop.get("visible") is not True or len(rows) < 5:
        return
    controls = live_controls(case)
    case.check("and the menu pad is not drawn under it",
               "menu-a" not in controls, "drawn: %s" % sorted(controls))
    case.shot("shop")

    before = shop_list(case)
    lit = next(r["label"] for r in rows[:3] if r["focused"])
    target_tab = "buy" if lit != "buy" else "sell"
    tap_touch_button(case, shop, touch_button(shop, "row", target_tab))
    switched = wait_game_menu(
        case, lambda m: any(r["focused"] and r["label"] == target_tab
                            for r in m.get("touch_menu", {}).get("rows", [])),
        10)
    after = shop_list(case)
    case.check("a tap on %s opened that tab in the game" % target_tab,
               any(r["focused"] and r["label"] == target_tab
                   for r in switched.get("touch_menu", {}).get("rows", [])),
               "was %s" % lit)
    case.check("and the game's list now holds that tab's entries",
               after.get("entries") != before.get("entries"),
               "%s -> %s" % (before.get("entries"), after.get("entries")))
    case.shot("tab")

    shop = touch_menu(case)
    rows = shop.get("rows", [])
    selected = after.get("selected", -1)
    entry = next((i - 3 for i, r in enumerate(rows)
                  if i >= 3 and r["clicks"] and i - 3 != selected), None)
    case.check("the tab has a shown entry besides the selected one",
               entry is not None, str([r["label"] for r in rows]))
    if entry is None:
        return
    button = next(b for b in shop["buttons"]
                  if b["part"] == "row" and b["index"] == entry + 3)
    tap_touch_button(case, shop, button)
    picked = wait_game_menu(
        case, lambda m: m.get("touch_menu", {}).get("focused_row") == entry + 3,
        10)
    case.check("a tap on %s selected that entry in the game"
               % rows[entry + 3]["label"],
               shop_list(case).get("selected") == entry
               and picked.get("touch_menu", {}).get("focused_row") == entry + 3,
               "selected %s" % shop_list(case).get("selected"))
    case.shot("selected")

    shown = touch_menu(case)
    accept = touch_button(shown, "footer", "accept")
    case.check("the shop offers the game's Accept footer", accept is not None,
               str([b["label"] for b in shown.get("buttons", [])
                    if b["part"] == "footer"]))
    if accept is None:
        return
    tap_touch_button(case, shown, accept)
    left = wait_game_menu(case, lambda m: m.get("menu") != "shop", 10)
    case.check("Accept left the shop",
               left.get("menu") != "shop", "now %r" % left.get("menu"))
    case.shot("after-accept")
    case.check("and the game is still running", case.alive())
