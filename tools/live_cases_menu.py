"""Live cases for the retail menus, dialogs and the touch menu."""

from __future__ import annotations

import json
import re
import shutil
import signal
import time

from live_game import live_controls, live_viewport, tap_control
from live_harness import Case
from live_menu import (
    MAIN_MENU_LABELS,
    OPTIONS_MENU_LABELS,
    MenuTimeline,
    drag_touch,
    keyboard_into_gameplay,
    keyboard_into_the_pda,
    keyboard_past_the_intro,
    menu_labels,
    menu_list,
    open_options,
    read_menu,
    row_labels,
    tap_touch_button,
    touch_button,
    touch_menu,
    touch_row,
    wait_game_menu,
    wait_menu,
    wait_movie_end,
    wait_row_change,
    wait_settled_touch_menu,
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

    The retail script temp_addmoney (setInventoryCount("MONEY", +2000)) gives
    the party money through the console's runscript. Each tap is judged by the
    game's own state: the lit tab, the list box's entries and selection, the
    money and potion counts, and the menu it has open.
    """
    case.prepare_profile(["boot.mode=continue", "input.touch_controls=2"])
    case.seed_save("autosave.save")
    case.launch({"X2_FILES": "1"})
    case.wait_control(60)
    menu = keyboard_into_gameplay(case, 300)
    case.check("the run reached gameplay through the PDA",
               menu.get("active") is False, str(menu.get("menu")))
    case.http("/console?command=runscript%20act1/genosha/genosha1/temp_addmoney")
    case.http("/console?command=openmenu%20shop")
    shop = wait_settled_touch_menu(case, "shop", 20)
    (case.dir / "shop.json").write_text(json.dumps(shop, indent=1) + "\n")
    tabs = shop.get("tabs", [])
    case.check("the touch menu is shown over the game's shop",
               shop.get("visible") is True
               and read_menu(case).get("class") == "CMenuShop",
               str(read_menu(case).get("class")))
    case.check("its tab bar is the game's three tabs",
               [t["label"] for t in tabs] == ["buy", "sell", "training"],
               str(tabs))
    case.check("and its rows are the list's entries",
               [r["label"] for r in shop.get("rows", [])]
               == menu_list(case).get("entries"),
               str([r["label"] for r in shop.get("rows", [])]))
    money = menu_labels(case).get("money_value")
    case.check("the money the script gave is shown as the game shows it",
               money == "2000" and {"label": "money", "value": "2000",
                                    "warn": False} in shop.get("facts", []),
               "money_value %r, facts %s" % (money, shop.get("facts")))
    case.check("and the selected entry's description",
               bool(shop.get("detail")), repr(shop.get("detail")))
    if shop.get("visible") is not True or len(tabs) != 3:
        return
    controls = live_controls(case)
    case.check("and the menu pad is not drawn under it",
               "menu-a" not in controls, "drawn: %s" % sorted(controls))
    case.shot("shop")

    lit = next((t["label"] for t in tabs if t["lit"]), None)
    # The game opens on buy and may step to training; a tab tap is judged on
    # whichever tab is not lit, and the purchase needs buy.
    targets = ["sell", "buy"] if lit == "buy" else ["buy"]
    for target_tab in targets:
        shown = touch_menu(case)
        tap_touch_button(case, shown, touch_button(shown, "tab", target_tab))
        switched = wait_game_menu(
            case, lambda m, t=target_tab: any(
                x["lit"] and x["label"] == t
                for x in m.get("touch_menu", {}).get("tabs", [])), 10)
        case.check("a tap on the %s tab opened it in the game" % target_tab,
                   any(t["lit"] and t["label"] == target_tab
                       for t in switched.get("touch_menu", {}).get("tabs", [])),
                   "was %s" % lit)
    case.shot("tab")

    shop = touch_menu(case)
    health = touch_button(shop, "row", "health pack")
    case.check("the buy tab offers a Health Pack", health is not None,
               str([r["label"] for r in shop.get("rows", [])]))
    if health is None:
        return
    tap_touch_button(case, shop, health)
    wait_game_menu(
        case, lambda m: m.get("touch_menu", {}).get("focused_row")
        == health["index"], 10)
    shop = touch_menu(case)
    row = shop.get("rows", [])[health["index"]]
    labels = menu_labels(case)
    cost = labels.get("item_cost_value", "")
    case.check("a tap on Health Pack selected it in the game",
               row["focused"] and menu_list(case).get("selected")
               == health["index"], "selected %s" % menu_list(case).get("selected"))
    case.check("and it shows the cost the game priced it at",
               row["value"] != "" and cost.endswith(row["value"]),
               "row %r, item_cost_value %r" % (row["value"], cost))
    case.shot("selected")

    before_money = int(labels.get("money_value", "0"))
    before_potions = int(labels.get("pot_health_value", "0"))
    price = int(row["value"] or "0")
    tap_touch_button(case, shop, touch_button(shop, "row", "health pack"))
    deadline = time.monotonic() + 10
    labels = menu_labels(case)
    while time.monotonic() < deadline \
            and labels.get("money_value") == str(before_money):
        time.sleep(0.3)
        labels = menu_labels(case)
    case.check("a tap on the selected Health Pack bought one",
               labels.get("money_value") == str(before_money - price)
               and labels.get("pot_health_value") == str(before_potions + 1),
               "money %d -> %s, health packs %d -> %s" % (
                   before_money, labels.get("money_value"), before_potions,
                   labels.get("pot_health_value")))
    shop = wait_game_menu(
        case, lambda m: {"label": "money",
                         "value": str(before_money - price), "warn": False}
        in m.get("touch_menu", {}).get("facts", []), 5).get("touch_menu", {})
    case.check("and the touch menu shows the new money",
               {"label": "money", "value": str(before_money - price),
                "warn": False} in shop.get("facts", []), str(shop.get("facts")))
    case.shot("bought")

    accept = touch_button(shop, "footer", "accept")
    case.check("the shop offers the game's Accept footer", accept is not None,
               str([b["label"] for b in shop.get("buttons", [])
                    if b["part"] == "footer"]))
    if accept is None:
        return
    tap_touch_button(case, shop, accept)
    left = wait_game_menu(case, lambda m: m.get("menu") != "shop", 10)
    case.check("Accept left the shop",
               left.get("menu") != "shop", "now %r" % left.get("menu"))
    case.shot("after-accept")
    case.check("and the game is still running", case.alive())


def case_touch_codex(case: Case) -> None:
    """The touch menu over CMenuCodex, opened by the console from gameplay.

    A tap on an entry walks the game's selection to it and accepts it, which
    loads it (0x005b1780); the Details footer opens its description. Each tap
    is judged by the game's own state: the list's selection, the codex mode
    and the loaded entry's name item.
    """
    case.prepare_profile(["boot.mode=continue", "input.touch_controls=2"])
    case.seed_save("autosave.save")
    case.launch({"X2_FILES": "1"})
    case.wait_control(60)
    menu = keyboard_into_gameplay(case, 300)
    case.check("the run reached gameplay through the PDA",
               menu.get("active") is False, str(menu.get("menu")))
    case.http("/console?command=openmenu%20codex")
    codex = wait_touch_menu(case, "codex", 20)
    (case.dir / "codex.json").write_text(json.dumps(codex, indent=1) + "\n")
    game = read_menu(case)
    case.check("the touch menu is shown over the game's codex list",
               codex.get("visible") is True
               and game.get("class") == "CMenuCodex" and game.get("mode") == 0,
               "%s mode %s" % (game.get("class"), game.get("mode")))
    entries = menu_list(case).get("entries", [])
    case.check("its rows are the list's entries",
               [r["label"] for r in codex.get("rows", [])] == entries
               and len(entries) > 4, str(entries))
    footers = [b["label"] for b in codex.get("buttons", [])
               if b["part"] == "footer"]
    case.check("and its footers are the game's Back and Details",
               footers == ["Back", "Details"], str(footers))
    if codex.get("visible") is not True or len(entries) <= 4:
        return
    case.shot("codex")

    target = entries[3]
    selected = menu_list(case).get("selected")
    case.check("the codex opened on another entry",
               selected != 3, "selected %s" % selected)
    tap_touch_button(case, codex, touch_button(codex, "row", target.lower()))
    wait_game_menu(case, lambda m: m.get("touch_menu", {}).get("focused_row")
                   == 3, 10)
    case.check("a tap on %s selected it in the game" % target,
               menu_list(case).get("selected") == 3,
               "selected %s" % menu_list(case).get("selected"))
    time.sleep(1.0)

    codex = touch_menu(case)
    tap_touch_button(case, codex, touch_button(codex, "footer", "details"))
    game = wait_game_menu(case, lambda m: m.get("mode") == 1, 10)
    reading = wait_game_menu(
        case, lambda m: m.get("touch_menu", {}).get("detail"),
        5).get("touch_menu", {})
    name = menu_labels(case).get("name", "")
    desc = menu_labels(case).get("desc", "")
    case.check("Details opened the description in the game",
               game.get("mode") == 1, "mode %s" % game.get("mode"))
    case.check("and the game loaded the tapped entry",
               name.endswith(target), "name %r" % name)
    case.check("the touch menu reads that entry's name and description",
               reading.get("title") == target and bool(reading.get("detail"))
               and reading["detail"][0] in desc,
               "title %r, first line %r" % (
                   reading.get("title"), (reading.get("detail") or [""])[0]))
    case.shot("reading")

    middle = reading["viewport_width"] * 0.5
    bottom = reading["viewport_height"] * 0.7
    drag_touch(case, reading, middle, bottom, bottom - 300.0)
    scrolled = wait_game_menu(
        case, lambda m: m.get("touch_menu", {}).get("scroll", 0) > 0,
        5).get("touch_menu", {})
    case.check("a drag scrolls the description and leaves the game's mode",
               scrolled.get("scroll", 0) > 0 and read_menu(case).get("mode") == 1,
               "scroll %s" % scrolled.get("scroll"))
    case.shot("scrolled")

    tap_touch_button(case, scrolled, touch_button(scrolled, "footer", "details"))
    game = wait_game_menu(case, lambda m: m.get("mode") == 0, 10)
    listed = wait_touch_menu(case, "codex", 5)
    case.check("Details again returned to the list on the same entry",
               game.get("mode") == 0 and listed.get("focused_row") == 3,
               "mode %s, focus %s" % (game.get("mode"), listed.get("focused_row")))

    back = touch_button(listed, "footer", "back")
    if back is None:
        case.check("the list offers Back", False)
        return
    tap_touch_button(case, listed, back)
    left = wait_game_menu(case, lambda m: m.get("menu") != "codex", 10)
    case.check("Back left the codex",
               left.get("menu") != "codex", "now %r" % left.get("menu"))
    case.shot("after-back")


def world_map_acts(case: Case) -> dict[str, bool]:
    """The game's act tabs that are unlocked, each with whether it is lit."""
    code, body = case.http("/menu?items=all")
    if code != 200:
        return {}
    acts = {}
    for item in json.loads(body).get("items", []):
        if re.fullmatch(r"option0\d_text", item["name"]) \
                and item["flags"] & 0x08:
            acts[item["label"]] = bool(item["flags"] & 0x01)
    return acts


def case_touch_worldmap(case: Case) -> None:
    """The touch menu over CMenuWorldMap, opened by the console from gameplay.

    Visiting a map with an extraction point unlocks it through its own load
    script (extractionUnlock), so the console's loadmap of savage1 and then
    sanctuary1 leaves one point open in each of acts 1 and 2. Each tap is
    judged by the game's own state: the lit act, the focused point, the menu
    it has open and the act the world map reopens on after travelling.
    """
    case.prepare_profile(["boot.mode=continue", "input.touch_controls=2"])
    case.seed_save("autosave.save")
    case.launch({"X2_FILES": "1"})
    case.wait_control(60)
    menu = keyboard_into_gameplay(case, 300)
    case.check("the run reached gameplay through the PDA",
               menu.get("active") is False, str(menu.get("menu")))
    for map_name in ("act2/savage/savage1", "act1/sanctuary/sanctuary1"):
        case.http("/console?command=loadmap%20" + map_name)
        time.sleep(5)
        menu = keyboard_into_gameplay(case, 300)
        case.check("loadmap %s reached gameplay" % map_name,
                   menu.get("active") is False, str(menu.get("menu")))
    case.http("/console?command=openmenu%20worldmap")
    shown = wait_touch_menu(case, "worldmap", 20)
    (case.dir / "worldmap.json").write_text(json.dumps(shown, indent=1) + "\n")
    case.check("the touch menu is shown over the game's world map",
               shown.get("visible") is True
               and read_menu(case).get("class") == "CMenuWorldMap",
               str(read_menu(case).get("class")))
    acts = world_map_acts(case)
    case.check("the game opened on act 1 with acts 1 and 2 unlocked",
               acts == {"act 1": True, "act 2": False}, str(acts))
    tabs = [(t["label"], t["lit"]) for t in shown.get("tabs", [])]
    case.check("its tabs are the unlocked acts with act 1 lit",
               tabs == [("act 1", True), ("act 2", False)], str(tabs))
    rows = [(r["label"], r["focused"]) for r in shown.get("rows", [])]
    case.check("its rows are act 1's unlocked point, focused",
               rows == [("Sanctuary", True)], str(rows))
    footers = [b["label"] for b in shown.get("buttons", [])
               if b["part"] == "footer"]
    case.check("its footers are one Back and go", footers == ["Back", "go"],
               str(footers))
    case.check("and the point's region and description",
               bool(shown.get("facts")) and bool(shown.get("detail")),
               "facts %s, detail %s" % (shown.get("facts"), shown.get("detail")))
    if shown.get("visible") is not True or len(tabs) != 2:
        return
    case.shot("worldmap")

    tap_touch_button(case, shown, touch_button(shown, "footer", "go"))
    time.sleep(3)
    case.check("go on the map the party is on stays in the world map",
               read_menu(case).get("menu") == "worldmap",
               str(read_menu(case).get("menu")))

    shown = touch_menu(case)
    tap_touch_button(case, shown, touch_button(shown, "tab", "act 2"))
    wait_game_menu(case, lambda m: any(
        t["lit"] and t["label"] == "act 2"
        for t in m.get("touch_menu", {}).get("tabs", [])), 10)
    acts = world_map_acts(case)
    shown = touch_menu(case)
    rows = [(r["label"], r["focused"]) for r in shown.get("rows", [])]
    case.check("a tap on act 2 opened it in the game",
               acts == {"act 1": False, "act 2": True}, str(acts))
    case.check("and the rows became act 2's point, focused",
               rows == [("Avalon", True)], str(rows))
    case.shot("act2")

    avalon = touch_button(shown, "row", "avalon")
    if avalon is None:
        return
    loading = 'front end menu "" -> "loading"'
    loads = case.log_text().count(loading)
    tap_touch_button(case, shown, avalon)
    left = wait_game_menu(case, lambda m: m.get("menu") != "worldmap", 10)
    case.check("a tap on the focused Avalon left the world map",
               left.get("menu") != "worldmap", str(left.get("menu")))
    deadline = time.monotonic() + 30
    while time.monotonic() < deadline \
            and case.log_text().count(loading) == loads:
        time.sleep(0.5)
    case.check("and the game loaded a map",
               case.log_text().count(loading) > loads)
    time.sleep(10)
    case.shot("travelled")
    case.http("/console?command=openmenu%20worldmap")
    shown = wait_touch_menu(case, "worldmap", 20)
    acts = world_map_acts(case)
    case.check("the world map reopens on act 2, the party's new act",
               acts == {"act 1": False, "act 2": True}, str(acts))
    back = touch_button(shown, "footer", "back")
    if back is None:
        case.check("the world map offers Back", False)
        return
    tap_touch_button(case, shown, back)
    left = wait_game_menu(case, lambda m: m.get("menu") != "worldmap", 10)
    case.check("Back left the world map",
               left.get("menu") != "worldmap", str(left.get("menu")))


def stash_tabs(case: Case) -> dict[str, bool]:
    """The game's stash tabs, each with whether it is lit."""
    code, body = case.http("/menu?items=all")
    if code != 200:
        return {}
    return {item["label"]: bool(item["flags"] & 0x01)
            for item in json.loads(body).get("items", [])
            if re.fullmatch(r"stash_option0\d", item["name"]) and item["label"]}


def case_touch_stash(case: Case) -> None:
    """The touch menu over CMenuShop as the stash, opened by the console.

    The party starts with no gear, so the case buys one piece in the shop by
    touch (two temp_addmoney runs pay for it), then stores it and takes it
    back. Each tap is judged by the game's own state: the lit tab, the list's
    entries and the stash and gear counts.
    """
    case.prepare_profile(["boot.mode=continue", "input.touch_controls=2"])
    case.seed_save("autosave.save")
    case.launch({"X2_FILES": "1"})
    case.wait_control(60)
    menu = keyboard_into_gameplay(case, 300)
    case.check("the run reached gameplay through the PDA",
               menu.get("active") is False, str(menu.get("menu")))
    for _ in range(2):
        case.http("/console?command=runscript%20"
                  "act1/genosha/genosha1/temp_addmoney")
    case.http("/console?command=openmenu%20shop")
    shop = wait_settled_touch_menu(case, "shop", 20)
    tap_touch_button(case, shop, touch_button(shop, "tab", "buy"))
    wait_game_menu(case, lambda m: any(
        t["lit"] and t["label"] == "buy"
        for t in m.get("touch_menu", {}).get("tabs", [])), 10)
    shop = touch_menu(case)
    gear = next((r for r in shop.get("rows", [])
                 if "Pack" not in r["label"] and r["label"] != "Grab Bag"), None)
    case.check("the buy tab offers gear", gear is not None,
               str([r["label"] for r in shop.get("rows", [])]))
    if gear is None:
        return
    name = gear["label"]
    tap_touch_button(case, shop, touch_button(shop, "row", name.lower()))
    wait_game_menu(case, lambda m: m.get("touch_menu", {}).get("focused_row")
                   == shop["rows"].index(gear), 10)
    before = menu_labels(case)
    shop = touch_menu(case)
    tap_touch_button(case, shop, touch_button(shop, "row", name.lower()))
    deadline = time.monotonic() + 10
    labels = menu_labels(case)
    while time.monotonic() < deadline \
            and labels.get("inventory_count") == before.get("inventory_count"):
        time.sleep(0.3)
        labels = menu_labels(case)
    case.check("a tap on the selected %s bought it" % name,
               labels.get("inventory_count") == "1/20"
               and labels.get("money_value") != before.get("money_value"),
               "gear %s -> %s, money %s -> %s" % (
                   before.get("inventory_count"), labels.get("inventory_count"),
                   before.get("money_value"), labels.get("money_value")))
    shop = touch_menu(case)
    tap_touch_button(case, shop, touch_button(shop, "footer", "accept"))
    wait_game_menu(case, lambda m: m.get("menu") != "shop", 10)

    case.http("/console?command=openmenu%20stash")
    stash = wait_settled_touch_menu(case, "stash", 20)
    if stash_tabs(case).get("stash"):
        # Opened on the stash tab and not stepped to inventory: open it.
        tap_touch_button(case, stash, touch_button(stash, "tab", "inventory"))
        wait_game_menu(case, lambda m: any(
            t["lit"] and t["label"] == "inventory"
            for t in m.get("touch_menu", {}).get("tabs", [])), 10)
        stash = wait_settled_touch_menu(case, "stash", 20)
    (case.dir / "stash.json").write_text(json.dumps(stash, indent=1) + "\n")
    case.check("the touch menu is shown over the game's stash",
               stash.get("visible") is True
               and read_menu(case).get("class") == "CMenuShop",
               str(read_menu(case).get("class")))
    tabs = [(t["label"], t["lit"]) for t in stash.get("tabs", [])]
    case.check("its tabs are stash and inventory with inventory lit",
               tabs == [("stash", False), ("inventory", True)], str(tabs))
    case.check("its rows are the party's gear",
               [r["label"] for r in stash.get("rows", [])] == [name]
               == menu_list(case).get("entries"),
               str([r["label"] for r in stash.get("rows", [])]))
    case.check("with the stash count and the gear's description",
               {"label": "stash", "value": "0/60", "warn": False}
               in stash.get("facts", []) and bool(stash.get("detail")),
               "facts %s, detail %s" % (stash.get("facts"), stash.get("detail")))
    if stash.get("visible") is not True or not stash.get("rows"):
        return
    case.shot("stash")

    tap_touch_button(case, stash, touch_button(stash, "row", name.lower()))
    stored = wait_game_menu(
        case, lambda m: not m.get("touch_menu", {}).get("rows"), 10)
    labels = menu_labels(case)
    case.check("a tap on the selected %s stored it" % name,
               labels.get("inventory_count") == "1/60"
               and not menu_list(case).get("entries"),
               "stash %s, inventory %s" % (labels.get("inventory_count"),
                                           menu_list(case).get("entries")))
    case.check("and the touch menu keeps the tabs over the empty list",
               len(stored.get("touch_menu", {}).get("tabs", [])) == 2,
               str(stored.get("touch_menu", {}).get("tabs")))
    case.shot("stored")

    stash = touch_menu(case)
    tap_touch_button(case, stash, touch_button(stash, "tab", "stash"))
    wait_game_menu(case, lambda m: [r["label"] for r in m.get(
        "touch_menu", {}).get("rows", [])] == [name], 10)
    stash = touch_menu(case)
    case.check("a tap on the stash tab opened it in the game",
               stash_tabs(case) == {"stash": True, "inventory": False}
               and [r["label"] for r in stash.get("rows", [])] == [name],
               "%s, rows %s" % (stash_tabs(case),
                                [r["label"] for r in stash.get("rows", [])]))
    case.shot("stash-tab")

    tap_touch_button(case, stash, touch_button(stash, "row", name.lower()))
    wait_game_menu(case, lambda m: not m.get("touch_menu", {}).get("rows"),
                   10)
    labels = menu_labels(case)
    case.check("a tap on it took it back to the inventory",
               labels.get("inventory_count") == "1/20",
               "gear %s" % labels.get("inventory_count"))
    stash = touch_menu(case)
    accept = touch_button(stash, "footer", "accept")
    if accept is None:
        case.check("the stash offers Accept", False)
        return
    tap_touch_button(case, stash, accept)
    left = wait_game_menu(case, lambda m: m.get("menu") != "stash", 10)
    case.check("Accept left the stash",
               left.get("menu") != "stash", str(left.get("menu")))


def review_tabs(case: Case) -> dict[str, bool]:
    """The game's review tabs, each with whether it is lit."""
    code, body = case.http("/menu?items=all")
    if code != 200:
        return {}
    return {item["label"]: bool(item["flags"] & 0x01)
            for item in json.loads(body).get("items", [])
            if re.fullmatch(r"option0\d_text", item["name"])}


def case_touch_review(case: Case) -> None:
    """The touch menu over CMenuReviewPaths, opened by the console.

    It opens on the stats tab. A tap on the cinematics tab opens it in the
    game, and two taps on Credits select it and press A, which plays the
    credits; Esc returns to the review and Back leaves it.
    """
    case.prepare_profile(["boot.mode=continue", "input.touch_controls=2"])
    case.seed_save("autosave.save")
    case.launch({"X2_FILES": "1"})
    case.wait_control(60)
    menu = keyboard_into_gameplay(case, 300)
    case.check("the run reached gameplay through the PDA",
               menu.get("active") is False, str(menu.get("menu")))
    case.http("/console?command=openmenu%20review")
    review = wait_settled_touch_menu(case, "review", 20)
    (case.dir / "review.json").write_text(json.dumps(review, indent=1) + "\n")
    case.check("the touch menu is shown over the game's review",
               review.get("visible") is True
               and read_menu(case).get("class") == "CMenuReviewPaths",
               str(read_menu(case).get("class")))
    tabs = [(t["label"], t["lit"]) for t in review.get("tabs", [])]
    case.check("its tabs are the game's five with stats lit",
               tabs == [("screens", False), ("cinematics", False),
                        ("comics", False), ("concepts", False),
                        ("stats", True)], str(tabs))
    rows = review.get("rows", [])
    game = menu_list(case)
    case.check("its rows are the stats entries with their counts",
               len(rows) == len(game.get("entries", [])) > 0
               and [r["value"] for r in rows] == game.get("values")
               and {"label": "Comic Books", "value": "0 of 3"}.items()
               <= rows[1].items(),
               str([(r["label"], r["value"]) for r in rows[:3]]))
    if review.get("visible") is not True or not rows:
        return
    case.shot("stats")

    tap_touch_button(case, review, touch_button(review, "tab", "cinematics"))
    review = wait_game_menu(case, lambda m: touch_row(
        m.get("touch_menu", {}), "credits") != {}, 10).get("touch_menu", {})
    case.check("a tap on the cinematics tab opened it in the game",
               review_tabs(case).get("cinematics") is True
               and touch_row(review, "credits") != {},
               "%s, rows %s" % (review_tabs(case),
                                [r["label"] for r in review.get("rows", [])]))
    case.shot("cinematics")

    if not touch_row(review, "credits").get("focused"):
        tap_touch_button(case, review, touch_button(review, "row", "credits"))
        review = wait_game_menu(case, lambda m: touch_row(
            m.get("touch_menu", {}), "credits").get("focused"),
            10).get("touch_menu", {})
    tap_touch_button(case, review, touch_button(review, "row", "credits"))
    credits = wait_game_menu(case, lambda m: m.get("menu") == "credits", 10)
    case.check("a tap on the selected Credits played them",
               credits.get("class") == "CMenuCredits"
               and credits.get("touch_menu", {}).get("visible") is False,
               "%s %s" % (credits.get("menu"), credits.get("class")))
    case.shot("credits")

    case.http("/key?name=Escape&hold=0.2")
    review = wait_touch_menu(case, "review", 10)
    case.check("Esc returned to the review under the touch menu",
               review.get("visible") is True, str(read_menu(case).get("menu")))
    back = touch_button(review, "footer", "back")
    if back is None:
        case.check("the review offers Back", False)
        return
    tap_touch_button(case, review, back)
    left = wait_game_menu(case, lambda m: m.get("menu") != "review", 10)
    case.check("Back left the review",
               left.get("menu") != "review", str(left.get("menu")))


def case_touch_region(case: Case) -> None:
    """The touch menu over CMenuRegion, opened by the console.

    With no online service the game's region list is empty; the touch menu
    shows the title and footers over it. A tap on Select opens the campaign
    lobby as the game's own A does, and Back on a reopened region leaves it.
    """
    case.prepare_profile(["boot.mode=continue", "input.touch_controls=2"])
    case.seed_save("autosave.save")
    case.launch({"X2_FILES": "1"})
    case.wait_control(60)
    menu = keyboard_into_gameplay(case, 300)
    case.check("the run reached gameplay through the PDA",
               menu.get("active") is False, str(menu.get("menu")))
    case.http("/console?command=openmenu%20region")
    region = wait_settled_touch_menu(case, "region", 20)
    (case.dir / "region.json").write_text(json.dumps(region, indent=1) + "\n")
    case.check("the touch menu is shown over the game's region list",
               region.get("visible") is True
               and read_menu(case).get("class") == "CMenuRegion",
               str(read_menu(case).get("class")))
    case.check("titled by the game's text_title",
               region.get("title") == "Region Menu", str(region.get("title")))
    rows = [r["label"] for r in region.get("rows", [])]
    game = menu_list(case, "text_list")
    case.check("its rows are the game's regions",
               "entries" in game and len(rows) == len(game["entries"]),
               "rows %s, game %s" % (rows, game.get("entries")))
    footers = [b["label"] for b in region.get("buttons", [])
               if b["part"] == "footer"]
    case.check("with the game's Back, Refresh and Select footers",
               footers == ["Back", "Refresh", "Select"], str(footers))
    if region.get("visible") is not True:
        return
    case.shot("region")

    tap_touch_button(case, region, touch_button(region, "footer", "select"))
    lobby = wait_game_menu(case, lambda m: m.get("menu") == "campaign_lobby",
                           10)
    case.check("a tap on Select opened the campaign lobby",
               lobby.get("class") == "CMenuCampaignLobby",
               "%s %s" % (lobby.get("menu"), lobby.get("class")))
    case.shot("lobby")

    case.http("/console?command=openmenu%20region")
    region = wait_touch_menu(case, "region", 20)
    back = touch_button(region, "footer", "back")
    if back is None:
        case.check("the region offers Back", False)
        return
    tap_touch_button(case, region, back)
    left = wait_game_menu(case, lambda m: m.get("menu") != "region", 10)
    case.check("Back left the region",
               left.get("menu") != "region", str(left.get("menu")))
