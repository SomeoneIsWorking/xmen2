"""Live cases for the retail pages native on touch that have no list of their
own (docs/touch-play.md).

Each boots into act2/jungle/jungle1 with touch forced on, taps through the
level's opening line, reaches its page by the route the case names, and
judges by GET /menu and GET /controls: no menu pad is drawn, every touch
button is a finger tall inside the window, and a tap does what the pad
would.
"""

from __future__ import annotations

import time
import urllib.parse

from live_cases_touch_map import LEVEL, drag, inside_window, into_level
from live_game import live_controls
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


def check_no_pad(case: Case, what: str) -> None:
    controls = live_controls(case)
    case.check("no menu pad is drawn over %s" % what,
               not any(name.startswith("menu-") for name in controls),
               "drawn: %s" % sorted(controls))


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
    check_no_pad(case, "the controller page")
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
    check_no_pad(case, "the image viewer")
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
    check_no_pad(case, "the credits")
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
    check_no_pad(case, "the personal file")
    check_finger_buttons(case, menu, "the personal file")
    tap_button_edge(case, footer_button(menu, "$MENU_BACK"))
    case.check("a tap on Back's edge closes it",
               closed(case, "CMenuPersonal"))
    case.shot("after")


PAGE_CASES.update({name: case_touch_personal for name in PERSONAL_SHAPES})
