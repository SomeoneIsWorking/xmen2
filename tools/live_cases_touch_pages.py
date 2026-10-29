"""Live cases for the retail pages native on touch that have no list of their
own (docs/touch-play.md).

Each boots into act2/jungle/jungle1 with touch forced on, taps through the
level's opening line, reaches its page by the route the case names, and
judges by GET /menu: every touch button is a finger tall inside the window,
and a tap does what the pad would.
"""

from __future__ import annotations

import time
import urllib.parse

from live_cases_touch_map import LEVEL, drag, inside_window, into_level
from live_harness import Case
from live_menu import (finger_tall, footer_button, keyboard_into_gameplay,
                       list_entry, menu_list, menu_text,
                       option_tabs, read_menu,
                       select_list_entry, tap_button_edge, tap_list_entry,
                       tap_tab, wait_list, wait_settled_menu)

PAGE_SHAPES = {
    "": (1280, 720),
    "-4x3": (960, 720),
    "-tablet": (1216, 800),
    "-20x9": (1600, 720),
}


def shapes(name: str) -> dict[str, tuple[int, int]]:
    return {name + suffix: size for suffix, size in PAGE_SHAPES.items()}


CONTROLLER_SHAPES = shapes("touch-controller-page")


def open_page(case: Case, menu: str, menu_class: str, ready) -> dict:
    """`openmenu menu` from gameplay, waited until `ready(menu)` holds."""
    case.http("/console?command=" + urllib.parse.quote("openmenu " + menu))
    return wait_page(case, menu_class, ready, 20)


def wait_page(case: Case, menu_class: str, ready, timeout: float) -> dict:
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        menu = read_menu(case)
        if menu.get("class") == menu_class and ready(menu):
            time.sleep(0.5)
            return read_menu(case)
        time.sleep(0.5)
    return read_menu(case)


def closed(case: Case, menu_class: str, timeout: float = 10) -> bool:
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        if read_menu(case).get("class") != menu_class:
            return True
        time.sleep(0.3)
    return False


def named_button(menu: dict, name: str) -> dict:
    return next((b for b in menu.get("touch_buttons", [])
                 if b.get("name") == name), {})


def check_finger_buttons(case: Case, menu: dict, what: str) -> None:
    buttons = menu.get("touch_buttons", [])
    case.check("%s: every touch button is a finger tall in the window" % what,
               bool(buttons) and all(finger_tall(b) and inside_window(b)
                                     for b in buttons),
               str([(b.get("name"), b.get("fingers")) for b in buttons]))


def case_touch_controller_page(case: Case) -> None:
    """CMenuOptionsController native on touch: its Back, which the game
    names and draws nowhere, is a finger button that closes the page."""
    case.check("the run reached the level's gameplay",
               into_level(case, *CONTROLLER_SHAPES[case.name]))
    menu = open_page(case, "options_controller", "CMenuOptionsController",
                     lambda m: named_button(m, "page prompt"))
    case.check("the game's controller page is up",
               menu.get("class") == "CMenuOptionsController",
               str(menu.get("class")))
    case.shot("open")
    back = named_button(menu, "page prompt")
    case.check("its Back is a finger button in the window",
               finger_tall(back) and inside_window(back),
               "%s" % back)
    check_finger_buttons(case, menu, "the controller page")
    if not back:
        return
    tap_button_edge(case, back)
    case.check("a tap on Back's edge closes the page",
               closed(case, "CMenuOptionsController"))
    case.shot("after")


PAGE_CASES = {name: case_touch_controller_page for name in CONTROLLER_SHAPES}


VIEWER_SHAPES = shapes("touch-image-viewer")


def swipe(case: Case, dx: float) -> None:
    """One finger across the middle of the window by `dx` of its width."""
    drag(case, {"x": 0.5 - dx / 2, "y": 0.45}, dx, 0.0)


def into_two_screens(case: Case, width: int, height: int) -> bool:
    """A Continue boot of autosave0, whose review holds the Prison Outpost
    loading screen, then the console's loadmap of the jungle, whose loading
    screen unlocks Garokk's, into gameplay in a width x height window."""
    case.prepare_profile(["boot.mode=continue", "input.touch_controls=2",
                          "video.width=%d" % width,
                          "video.height=%d" % height,
                          "video.mode=windowed"])
    case.seed_save("autosave0.save")
    case.launch({"X2_FILES": "1"})
    case.wait_control(60)
    if keyboard_into_gameplay(case, 300).get("active") is not False:
        return False
    case.http("/console?command=loadmap%20" + urllib.parse.quote(LEVEL))
    time.sleep(5)
    return keyboard_into_gameplay(case, 300).get("active") is False


def viewer(case: Case) -> dict:
    return read_menu(case).get("viewer") or {}


def wait_viewer(case: Case, done, timeout: float = 10) -> dict:
    deadline = time.monotonic() + timeout
    shown = viewer(case)
    while time.monotonic() < deadline and not done(shown):
        time.sleep(0.3)
        shown = viewer(case)
    return shown


def open_screen(case: Case) -> dict:
    """The review's screens tab and its first unlocked screen accepted by
    two taps: the image viewer showing it."""
    case.http("/console?command=openmenu%20review")
    wait_settled_menu(case, "review", 20)
    tap_tab(case, r"option0\d_text", "screens")
    deadline = time.monotonic() + 10
    while (time.monotonic() < deadline
           and not option_tabs(case, r"option0\d_text").get("screens")):
        time.sleep(0.3)
    time.sleep(1.0)
    box = menu_list(case)
    entries = [menu_text(e) for e in box.get("entries", [])]
    unlocked = [i for i, e in enumerate(entries) if not e.startswith("[")]
    case.check("the review's screens tab lists unlocked screens",
               len(unlocked) >= 2, str(entries))
    if len(unlocked) < 2:
        return {}
    select_list_entry(case, unlocked[0])
    tap_list_entry(case, unlocked[0])
    return wait_page(case, "CMenuImageViewer",
                     lambda m: len(m.get("touch_buttons", [])) >= 3, 20)


def case_touch_image_viewer(case: Case) -> None:
    """CMenuImageViewer native on touch, reached from the review: Back,
    Previous and Next are finger buttons, Next and Previous step the
    review's screens and Back closes the viewer."""
    case.check("the run reached the jungle's gameplay from the autosave",
               into_two_screens(case, *VIEWER_SHAPES[case.name]))
    menu = open_screen(case)
    case.check("the game's image viewer is up",
               menu.get("class") == "CMenuImageViewer", str(menu.get("class")))
    if menu.get("class") != "CMenuImageViewer":
        case.shot("no-viewer")
        return
    case.shot("open")
    check_finger_buttons(case, menu, "the image viewer")
    first = viewer(case)
    case.check("it shows a review screen", first.get("review_mode") == 0
               and bool(first.get("image")), str(first))
    tap_button_edge(case, footer_button(menu, "$MENU_NEXT"))
    shown = wait_viewer(case, lambda v: v.get("image") != first.get("image"))
    case.check("a tap on Next shows another screen",
               shown.get("image") != first.get("image"),
               "%s -> %s" % (first, shown))
    case.shot("next")
    tap_button_edge(case, footer_button(read_menu(case), "$MENU_PREV"))
    back_to = wait_viewer(case, lambda v: v.get("image") == first.get("image"))
    case.check("a tap on Previous shows the first again",
               back_to.get("image") == first.get("image"), str(back_to))
    swipe(case, -0.4)
    swiped = wait_viewer(case, lambda v: v.get("image") != first.get("image"))
    case.check("a swipe to the left shows the next screen",
               swiped.get("image") == shown.get("image"), str(swiped))
    swipe(case, 0.4)
    swiped = wait_viewer(case, lambda v: v.get("image") == first.get("image"))
    case.check("a swipe to the right shows the previous screen",
               swiped.get("image") == first.get("image"), str(swiped))
    tap_button_edge(case, footer_button(read_menu(case), "$MENU_BACK"))
    case.check("a tap on Back closes the viewer",
               closed(case, "CMenuImageViewer"))
    case.shot("after")


PAGE_CASES.update({name: case_touch_image_viewer for name in VIEWER_SHAPES})


CREDITS_SHAPES = shapes("touch-credits")


def case_touch_credits(case: Case) -> None:
    """CMenuCredits native on touch, reached from the review's cinematics:
    the credits roll with no pad over them and a tap ends them, as A or B
    does, back to the review."""
    case.check("the run reached the level's gameplay",
               into_level(case, *CREDITS_SHAPES[case.name]))
    case.http("/console?command=openmenu%20review")
    wait_settled_menu(case, "review", 20)
    tap_tab(case, r"option0\d_text", "cinematics")
    box = wait_list(case, lambda b: list_entry(b, "credits") >= 0)
    credits = list_entry(box, "credits")
    case.check("the review's cinematics list Credits", credits >= 0,
               str(box.get("entries")))
    if credits < 0:
        return
    select_list_entry(case, credits)
    tap_list_entry(case, credits)
    menu = wait_page(case, "CMenuCredits", lambda m: True, 15)
    case.check("a tap on the selected Credits rolls the credits",
               menu.get("class") == "CMenuCredits", str(menu.get("class")))
    if menu.get("class") != "CMenuCredits":
        return
    time.sleep(3.0)
    case.shot("rolling")
    case.check("the credits are still rolling 3 s on",
               read_menu(case).get("class") == "CMenuCredits")
    case.http("/touch?x=0.5&y=0.5")
    case.check("a tap on the credits ends them",
               closed(case, "CMenuCredits"))
    back = wait_page(case, "CMenuReviewPaths", lambda m: True, 10)
    case.check("back to the review", back.get("class") == "CMenuReviewPaths",
               str(back.get("class")))
    case.shot("after")


PAGE_CASES.update({name: case_touch_credits for name in CREDITS_SHAPES})


PERSONAL_SHAPES = shapes("touch-personal")
# The install's one personal file (Data/personal/wolverine01.XMLB).
PERSONAL_FILE = "wolverine01"


def case_touch_personal(case: Case) -> None:
    """CMenuPersonal native on touch, opened by the game's personalItem
    script function with the install's one file: no pad over it and Back a
    finger button that closes it."""
    case.check("the run reached the level's gameplay",
               into_level(case, *PERSONAL_SHAPES[case.name]))
    case.http("/script?text=" + urllib.parse.quote(
        "personalItem('%s')" % PERSONAL_FILE))
    menu = wait_page(case, "CMenuPersonal",
                     lambda m: footer_button(m, "$MENU_BACK"), 20)
    case.check("the game's personal file is up",
               menu.get("class") == "CMenuPersonal", str(menu.get("class")))
    if menu.get("class") != "CMenuPersonal":
        return
    case.shot("open")
    check_finger_buttons(case, menu, "the personal file")
    tap_button_edge(case, footer_button(menu, "$MENU_BACK"))
    case.check("a tap on Back's edge closes it",
               closed(case, "CMenuPersonal"))
    case.shot("after")


PAGE_CASES.update({name: case_touch_personal for name in PERSONAL_SHAPES})


ADVANCED_SHAPES = shapes("touch-advanced-options")


def sebas_widget(menu: dict, kind: str, text: str = "",
                 dialog: bool = False) -> dict:
    """Advanced Options' widget of `kind` showing `text` (any, when empty),
    in the front dialog when `dialog`."""
    windows = menu.get("sebas_dialogs", []) if dialog else [menu.get("sebas")]
    if not windows or not windows[0]:
        return {}
    return next((w for w in windows[0]["widgets"]
                 if w["kind"] == kind and w["shown"]
                 and (not text or w["text"].lower() == text.lower())), {})


def finger_target(widget: dict) -> bool:
    return (bool(widget) and widget.get("fingers", 0.0) >= 1.0 - 1e-4
            and widget.get("fingers_wide", 0.0) >= 1.0 - 1e-4
            and inside_window(widget))


def tap_edge(case: Case, widget: dict) -> bool:
    edge = widget.get("edge")
    return edge is not None and case.http(
        "/touch?x=%g&y=%g" % (edge["x"], edge["y"]))[0] == 200


def open_advanced(case: Case) -> dict:
    """Options' Advanced... tapped at its edge; Advanced Options settled."""
    menu = open_page(case, "options", "CMenuOptions",
                     lambda m: footer_button(m, "$MENU_OK"))
    tap_button_edge(case, footer_button(menu, "$MENU_OK"))
    return wait_sebas(case, lambda m: True)


def wait_sebas(case: Case, ready, timeout: float = 15) -> dict:
    """Advanced Options up, its window settled and `ready(menu)`."""
    return wait_page(case, "CMenuSebas",
                     lambda m: bool(m.get("sebas"))
                     and not m["sebas"]["animating"] and ready(m), timeout)


def dialog_up(menu: dict) -> bool:
    return bool(menu.get("sebas_dialogs"))


def apart(widgets: list[dict]) -> bool:
    """No two of `widgets`' client rectangles share a pixel."""
    rects = [w["client"] for w in widgets]
    return all(a[2] < b[0] or b[2] < a[0] or a[3] < b[1] or b[3] < a[1]
               for i, a in enumerate(rects) for b in rects[i + 1:])


def check_answers(case: Case, menu: dict, what: str) -> list[dict]:
    """The front dialog's Yes, No and Back are finger targets apart."""
    rows = [sebas_widget(menu, "value", text, dialog=True)
            for text in ("Yes", "No")]
    back = sebas_widget(menu, "text_button", "Back", dialog=True)
    widgets = [*rows, back]
    case.check("%s, with finger rows and a Back apart" % what,
               all(finger_target(w) for w in widgets) and apart(widgets),
               "rows %s back %s" % (rows, back))
    return rows


def case_touch_advanced_options(case: Case) -> None:
    """Advanced Options (CMenuSebas) on touch: Options' Advanced... opens
    it, no pad is drawn, every control is a finger target in the window, a
    tab, a drag and a tap on the key table, the slider's finger band, a
    dialog's rows and its Back, and the screen's own Back and Accept close
    it (issue #199)."""
    case.check("the run reached the level's gameplay",
               into_level(case, *ADVANCED_SHAPES[case.name]))
    menu = open_page(case, "options", "CMenuOptions",
                     lambda m: footer_button(m, "$MENU_OK"))
    advanced = footer_button(menu, "$MENU_OK")
    case.check("Options' Advanced... is a finger button in the window",
               finger_tall(advanced) and inside_window(advanced),
               str(advanced))
    tap_button_edge(case, advanced)
    sebas = wait_sebas(case, lambda m: True)
    case.check("a tap on its edge opens Advanced Options",
               sebas.get("class") == "CMenuSebas", str(sebas.get("class")))
    time.sleep(1.0)
    case.shot("advanced")
    targets = [("tab", "Player 1"), ("tab", "Player 2"), ("tab", "Player 3"),
               ("tab", "Player 4"), ("text_button", "defaults 1"),
               ("text_button", "defaults 2"), ("text_button", "defaults 3"),
               ("button", "Accept"), ("text_button", "Back"),
               ("text_button", "Revert to default"), ("cycle", "FSAA")]
    small = ["%s %s" % target for target in targets
             if not finger_target(sebas_widget(sebas, *target))]
    case.check("every control is a finger target in the window", not small,
               "small or missing: %s" % small)

    tap_edge(case, sebas_widget(sebas, "tab", "Player 2"))
    shown = wait_sebas(case, lambda m: m.get("sebas_table", {})
                       .get("player") == 1, 5)
    case.check("a tap on Player 2's edge shows player 2's keys",
               shown.get("sebas_table", {}).get("player") == 1,
               str(shown.get("sebas_table")))
    tap_edge(case, sebas_widget(shown, "tab", "Player 1"))
    wait_sebas(case, lambda m: m.get("sebas_table", {}).get("player") == 0, 5)

    table = sebas_widget(sebas, "table")
    drag(case, table["tap"], 0.0, -0.2)
    scrolled = wait_sebas(case, lambda m: m.get("sebas_table", {})
                          .get("scroll", 0.0) > 0.0, 5)
    case.shot("scrolled")
    case.check("a drag up the key table scrolls it down",
               scrolled.get("sebas_table", {}).get("scroll", 0.0) > 0.0,
               str(scrolled.get("sebas_table")))
    case.http("/touch?x=%g&y=%g" % (table["tap"]["x"], table["tap"]["y"]))
    time.sleep(1.0)
    tapped = read_menu(case)
    case.check("a tap on a key cell does not wait for a key",
               tapped.get("sebas_table", {}).get("capture_column") == -1,
               str(tapped.get("sebas_table")))

    slider = sebas_widget(sebas, "slider")
    labels = [w["text"] for w in sebas["sebas"]["widgets"]
              if w["kind"] == "label"]
    height = slider["client"][3] - slider["client"][1] + 1
    below = {"x": slider["tap"]["x"] * 0.6 + slider["edge"]["x"] * 0.4,
             "y": slider["tap"]["y"] + (slider["tap"]["y"]
                                        - slider["edge"]["y"]) * 2.5}
    case.http("/touch?x=%g&y=%g" % (below["x"], below["y"]))
    time.sleep(1.0)
    stepped = read_menu(case)
    after = [w["text"] for w in stepped["sebas"]["widgets"]
             if w["kind"] == "label"]
    case.check("a tap in the slider's finger band, below its line, moves it",
               after != labels, "%s -> %s (slider %d px tall)"
               % (labels, after, height))

    tap_edge(case, sebas_widget(stepped, "text_button", "Revert to default"))
    asked = wait_sebas(case, dialog_up, 5)
    case.shot("revert-dialog")
    rows = check_answers(case, asked, "Revert asks")
    tap_edge(case, rows[1])
    answered = wait_sebas(case, lambda m: not dialog_up(m), 5)
    case.check("its No closes the question and keeps Advanced Options",
               not dialog_up(answered), str(answered.get("sebas_dialogs")))

    tap_edge(case, sebas_widget(answered, "text_button", "defaults 1"))
    time.sleep(1.0)
    tap_edge(case, sebas_widget(answered, "text_button", "Back"))
    warned = wait_sebas(case, dialog_up, 5)
    case.shot("cancel-dialog")
    case.check("Back after Defaults 1 asks to drop the change",
               dialog_up(warned), str(warned.get("sebas_dialogs")))
    rows = check_answers(case, warned, "it asks")
    no = rows[1]["edge"]
    case.http("/touch?x=%g&y=%g&phase=down" % (no["x"], no["y"]))
    time.sleep(1.0)
    pressed = read_menu(case)
    case.shot("no-pressed")
    bars = [w["client"] for w in pressed.get("sebas_dialogs", [{}])[0]
            .get("widgets", []) if w["kind"] == "image"]
    case.check("a finger on No brings the highlight onto it",
               rows[1]["client"] in bars, "images %s" % bars)
    case.http("/touch?x=%g&y=%g&phase=up" % (no["x"], no["y"]))
    kept = wait_sebas(case, lambda m: not dialog_up(m), 5)
    case.check("its No keeps Advanced Options", not dialog_up(kept),
               str(kept.get("sebas_dialogs")))
    tap_edge(case, sebas_widget(kept, "text_button", "Back"))
    warned = wait_sebas(case, dialog_up, 5)
    tap_edge(case, sebas_widget(warned, "text_button", "Back", dialog=True))
    kept = wait_sebas(case, lambda m: not dialog_up(m), 5)
    case.check("the question's Back keeps Advanced Options",
               not dialog_up(kept), str(kept.get("sebas_dialogs")))
    tap_edge(case, sebas_widget(kept, "text_button", "Back"))
    warned = wait_sebas(case, dialog_up, 5)
    tap_edge(case, sebas_widget(warned, "value", "Yes", dialog=True))
    case.check("its Yes leaves Advanced Options", closed(case, "CMenuSebas"))
    case.shot("left")

    sebas = open_advanced(case)
    tap_edge(case, sebas_widget(sebas, "button", "Accept"))
    case.check("Accept closes it", closed(case, "CMenuSebas"))
    sebas = open_advanced(case)
    tap_edge(case, sebas_widget(sebas, "text_button", "Back"))
    case.check("Back with nothing changed closes it",
               closed(case, "CMenuSebas"))


PAGE_CASES.update({name: case_touch_advanced_options
                   for name in ADVANCED_SHAPES})


def case_menu_action_route(case: Case) -> None:
    """GET /menu/action?name=ok posted while Options is up reaches its own
    update, which opens the real Advanced Options; with no menu up it is
    refused."""
    case.check("the run reached the level's gameplay",
               into_level(case, 1280, 720))
    code, body = case.http("/menu/action?name=ok")
    case.check("with no menu up the route refuses", code == 409,
               "%d %s" % (code, body.decode(errors="replace").strip()))
    open_page(case, "options", "CMenuOptions",
              lambda m: footer_button(m, "$MENU_OK"))
    code, body = case.http("/menu/action?name=ok")
    case.check("it posts $MENU_OK to Options", code == 200
               and b"CMenuOptions" in body,
               body.decode(errors="replace").strip())
    routed = wait_page(case, "CMenuSebas", lambda m: True, 15)
    case.check("and Options opens Advanced Options",
               routed.get("menu") == "sebas"
               and routed.get("class") == "CMenuSebas",
               "%s %s" % (routed.get("menu"), routed.get("class")))
    case.shot("routed")


PAGE_CASES["menu-action-route"] = case_menu_action_route
