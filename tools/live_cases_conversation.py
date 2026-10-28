"""Live case for a conversation on touch (docs/touch-play.md).

The run continues the tutorial autosave into its opening conversation, a
line and one "continue..." response at a time, and taps that response's card
until the conversation ends and gameplay's controls are back. It then loads
the Sanctuary hub, starts Cyclops' conversation there (1_sanctuary1_0150) with
startConversation, taps through to the player's question list, taps the
second question and judges by the game's own state that the conversation
went down that branch: the line shown next is one the second question leads
to and not the first. It leaves by the list's last question, the goodbye. Every conversation screen is
judged by GET /menu: no menu pad drawn, the line inside the panel, each
response a finger-tall card inside the panel and clear of every other.
"""

from __future__ import annotations

import json
import time
import urllib.parse

from live_cases_use_prompt import press
from live_game import live_controls, live_viewport
from live_harness import Case
from live_menu import finger_tall, keyboard_into_gameplay, read_menu

# The window each variant runs in: a tablet (a 2800x1840 panel's
# presentation), 4:3, 16:9 and a 20:9 phone.
SHAPES = {
    "touch-conversation": (1096, 720),
    "touch-conversation-4x3": (960, 720),
    "touch-conversation-16x9": (1280, 720),
    "touch-conversation-20x9": (1600, 720),
}
# startConversation only runs a conversation of the loaded level.
HUB = "act1/sanctuary/sanctuary1"
CHOICES = "act1/sanctuary/1_sanctuary1_0150"


def conversation(case: Case) -> dict | None:
    return read_menu(case).get("conversation")


def wait_line(case: Case, timeout: float, after: int | None = None) -> dict:
    """The conversation once its touch layout is drawn and a tap would pick,
    on a line other than `after`; {} when none is."""
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        shown = conversation(case)
        if shown and shown.get("touch") and shown.get("accepts") \
                and shown.get("responses") and shown.get("line_id") != after:
            return shown
        time.sleep(0.3)
    return {}


def wait_gone(case: Case, timeout: float) -> bool:
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        if conversation(case) is None:
            return True
        time.sleep(0.3)
    return False


def inside(low: dict, high: dict, outer_low: dict, outer_high: dict) -> bool:
    """A window-fraction box inside another; y grows down the window."""
    return (outer_low["x"] <= low["x"] + 1e-4
            and high["x"] <= outer_high["x"] + 1e-4
            and outer_high["y"] <= high["y"] + 1e-4
            and low["y"] <= outer_low["y"] + 1e-4)


def overlap(a: dict, b: dict) -> bool:
    return (a["low"]["x"] < b["high"]["x"] and b["low"]["x"] < a["high"]["x"]
            and a["high"]["y"] < b["low"]["y"]
            and b["high"]["y"] < a["low"]["y"])


def judge(case: Case, shown: dict, what: str) -> None:
    """The touch layout of one conversation screen."""
    controls = live_controls(case)
    case.check("%s: no menu pad is drawn" % what,
               not any(name.startswith("menu-") for name in controls),
               "drawn: %s" % sorted(controls))
    panel = (shown.get("panel_low"), shown.get("panel_high"))
    text = (shown.get("text_low"), shown.get("text_high"))
    case.check("%s: the line is inside the panel" % what,
               None not in panel and None not in text
               and inside(*text, *panel), "%s in %s" % (text, panel))
    cards = shown.get("responses", [])
    case.check("%s: %d response(s), each a finger-tall card in the panel"
               % (what, len(cards)),
               bool(cards) and None not in panel
               and all(finger_tall(card) and inside(card["low"], card["high"],
                                                    *panel)
                       for card in cards),
               json.dumps([(c["text"], c.get("fingers")) for c in cards]))
    clashes = [(a["text"], b["text"]) for i, a in enumerate(cards)
               for b in cards[i + 1:] if overlap(a, b)]
    case.check("%s: the cards are disjoint" % what, not clashes, str(clashes))
    case.check("%s: every card sits below the line" % what,
               None not in text
               and all(card["high"]["y"] >= text[0]["y"] - 1e-4
                       for card in cards))


def tap(case: Case, card: dict) -> bool:
    return press(case, card["tap"]["x"], card["tap"]["y"])


def gameplay_back(case: Case) -> bool:
    deadline = time.monotonic() + 20
    while time.monotonic() < deadline:
        controls = live_controls(case)
        if controls and not any(name.startswith("menu-")
                                for name in controls):
            return True
        time.sleep(0.5)
    return False


def case_touch_conversation(case: Case) -> None:
    width, height = SHAPES[case.name]
    case.prepare_profile(["boot.mode=continue", "input.touch_controls=2",
                          "video.width=%d" % width,
                          "video.height=%d" % height,
                          "video.mode=windowed"])
    case.seed_save("autosave0.save")
    case.launch({"X2_FILES": "1"})
    case.wait_control(60)

    shown = wait_line(case, 120)
    case.check("the opening conversation is up with its touch layout",
               bool(shown), json.dumps(shown)[:200])
    if not shown:
        case.shot("no-conversation")
        return
    case.check("its first line is Cyclops' to Nightcrawler",
               shown.get("speaker") == "Cyclops"
               and "Nightcrawler" in shown.get("line", ""),
               "%r: %r" % (shown.get("speaker"), shown.get("line")))
    judge(case, shown, "the opening line")
    case.check("a continue-only line is one card",
               len(shown.get("responses", [])) == 1,
               str([c["text"] for c in shown.get("responses", [])]))
    case.shot("opening")
    (case.dir / "opening.json").write_text(json.dumps(shown, indent=1) + "\n")

    lines = 0
    while shown and lines < 12:
        lines += 1
        case.check("opening line %d: a tap on its card is delivered" % lines,
                   tap(case, shown["responses"][0]))
        shown = wait_line(case, 8, shown.get("line_id"))
    case.check("the opening conversation ended after %d line(s)" % lines,
               wait_gone(case, 20))
    case.check("gameplay's controls are back", gameplay_back(case),
               "drawn: %s" % sorted(live_controls(case)))
    # The overlay publishes its surface with the controls it draws.
    window = live_viewport(case)
    case.check("the window is %dx%d" % (width, height),
               window is not None and tuple(window) == (width, height),
               str(window))
    case.shot("after-opening")

    case.http("/console?command=loadmap%20" + urllib.parse.quote(HUB))
    time.sleep(5)
    keyboard_into_gameplay(case, 300)
    case.http("/console?command=runscript%20" + urllib.parse.quote(
        'startConversation("%s")' % CHOICES))
    shown = wait_line(case, 20)
    steps = 0
    while shown and len(shown.get("responses", [])) < 2 and steps < 6:
        steps += 1
        tap(case, shown["responses"][0])
        shown = wait_line(case, 8, shown.get("line_id"))
    cards = shown.get("responses", []) if shown else []
    case.check("Cyclops' conversation reached its question list",
               len(cards) >= 2, json.dumps(shown)[:300])
    if len(cards) < 2:
        case.shot("no-choices")
        return
    judge(case, shown, "the question list")
    case.shot("choices")
    (case.dir / "choices.json").write_text(json.dumps(shown, indent=1) + "\n")

    first, second = cards[0], cards[1]
    case.check("a tap on the second question is delivered",
               tap(case, second))
    picked = wait_line(case, 8, shown.get("line_id"))
    line_id = picked.get("line_id")
    case.check("the conversation went down the second question's branch",
               line_id in second.get("next", [])
               and line_id not in first.get("next", []),
               "line %s; second leads to %s, first to %s"
               % (line_id, second.get("next"), first.get("next")))
    log = case.log_path.read_text(errors="replace")
    case.check("the game log names the tapped question",
               'a tap picks response 1 of %d' % len(cards) in log
               and second["text"] in log)
    case.shot("branch")

    steps = 0
    shown = picked
    while shown and steps < 12:
        steps += 1
        tap(case, shown["responses"][-1])
        shown = wait_line(case, 8, shown.get("line_id"))
    case.check("Cyclops' conversation ended by the goodbye",
               wait_gone(case, 20))
    case.check("gameplay's controls are back after it", gameplay_back(case),
               "drawn: %s" % sorted(live_controls(case)))
    case.shot("after")


def skip_drawn(case: Case) -> bool:
    return any(line.startswith("skip button")
               for line in case.get_text("/controls").splitlines())


def case_touch_cinematic(case: Case) -> None:
    """The tutorial's opening cinematic on touch, from the autosave's Continue
    to gameplay: the stretches with no conversation up and every line of the
    conversations inside it, each line tapped on its card. Sampled four times
    a second, no screen of it draws the menu pad, the cinematic offers its
    Skip, and gameplay's controls come back at its end."""
    case.prepare_profile(["boot.mode=continue", "input.touch_controls=2",
                          "video.width=1280", "video.height=720",
                          "video.mode=windowed"])
    case.seed_save("autosave0.save")
    case.launch({"X2_FILES": "1"})
    case.wait_control(60)

    samples = {"cinematic": 0, "conversation": 0}
    padded = {"cinematic": [], "conversation": []}
    skip_seen = 0
    lines_tapped = 0
    conversation_seen = False
    gap_shot = False
    last_tap = 0.0
    gameplay = False
    deadline = time.monotonic() + 240
    while time.monotonic() < deadline:
        menu = read_menu(case)
        controls = live_controls(case)
        if menu.get("active"):
            time.sleep(0.25)
            continue
        if any(not name.startswith("menu-") for name in controls):
            if conversation_seen:
                gameplay = True
                break
            time.sleep(0.25)
            continue
        pad = sorted(name for name in controls if name.startswith("menu-"))
        shown = menu.get("conversation")
        kind = "conversation" if shown else "cinematic"
        samples[kind] += 1
        if pad:
            padded[kind].append(pad)
        skip_seen += skip_drawn(case)
        if shown:
            conversation_seen = True
        elif conversation_seen:
            if not gap_shot:
                case.shot("between-lines")
                gap_shot = True
        if shown and shown.get("touch") and shown.get("accepts") \
                and shown.get("responses") \
                and time.monotonic() - last_tap > 1.5:
            if lines_tapped == 0:
                case.shot("line")
            if tap(case, shown["responses"][0]):
                lines_tapped += 1
            last_tap = time.monotonic()
        time.sleep(0.25)

    case.check("the opening's stretches with no conversation up were seen",
               samples["cinematic"] > 0, str(samples))
    case.check("no cinematic stretch draws the menu pad",
               not padded["cinematic"],
               "%d of %d sample(s) drew %s" % (
                   len(padded["cinematic"]), samples["cinematic"],
                   padded["cinematic"][:1]))
    case.check("no conversation line draws the menu pad",
               samples["conversation"] > 0 and not padded["conversation"],
               "%d of %d sample(s) drew it" % (len(padded["conversation"]),
                                               samples["conversation"]))
    case.check("the opening's lines were tapped through on their cards",
               lines_tapped >= 3, "%d tap(s)" % lines_tapped)
    case.check("the cinematic offers its Skip", skip_seen > 0,
               "%d sample(s)" % skip_seen)
    case.check("gameplay's controls are back after the cinematic", gameplay,
               "drawn: %s" % sorted(live_controls(case)))
    case.shot("gameplay")
