#!/usr/bin/env python3
"""Launch and drive a bounded live x2native run for one named scenario.

This is an agent-owned harness. It never invokes run.sh or tools/run.py: it
starts build/native/x2native DIRECTLY, on an isolated profile, with a
dedicated control port, and talks to that port itself rather than through
scratch/run/live.json.

    tools/live_case.py cutscene-skip          # Escape skips the tutorial scene
    tools/live_case.py cutscene-skip-early    # ... pressed in the camera-only pan
    tools/live_case.py boot-continue          # Boot=Continue reaches the saved map
    tools/live_case.py pad-late               # pad attached after start works
    tools/live_case.py pad-persisted          # stored controller0 id adopted
    tools/live_case.py extraction-off         # a pad does nothing for the party
    tools/live_case.py extraction-free        # a pad revives and refills the party
    tools/live_case.py extraction-paid        # near a pad, a request pays the total
    tools/live_case.py menu-model             # GET /menu matches the drawn menus

Every case prints PASS/FAIL evidence lines and exits 0 only on a full pass.
Artifacts (log, screenshots, profile) stay under
scratch/run/cases/<case>/<port>/, so concurrent runs on different ports never
share a profile or a log.
The run is killed BY PID at the end; nothing is left running.
"""

from __future__ import annotations

import argparse
import sys
from pathlib import Path

from live_cases_boot import (
    case_boot_continue,
    case_cutscene_skip,
    case_cutscene_skip_early,
    case_manual_continue,
)
from live_cases_menu import (
    case_menu_model,
    case_menu_pad,
    case_menu_touch,
    case_options_back,
    case_selector_dialog,
)
from live_cases_pad import (
    case_deadzone_render,
    case_deadzone_water,
    case_pad_after_load,
    case_pad_late,
    case_pad_persisted,
    case_stick_travel,
    case_touch_pad,
)
from live_cases_touch_menu import (
    case_touch_danger_room,
    case_touch_roster,
    case_touch_codex,
    case_touch_menu,
    case_touch_region,
    case_touch_review,
    case_touch_shop,
    case_touch_stash,
    case_touch_team,
    case_touch_worldmap,
)
from live_cases_touch_team import (
    case_touch_team_ai,
    case_touch_team_assign,
    case_touch_team_gear,
    case_touch_team_skill_details,
    case_touch_team_skills,
    case_touch_team_stats,
)
from live_cases_extraction import (
    case_extract_keyboard_back,
    case_extraction_free,
    case_extraction_off,
    case_extraction_paid,
)
from live_harness import BINARY, DEFAULT_PORT, Case, RunOptions, refuse

CASES = {
    "cutscene-skip": case_cutscene_skip,
    "cutscene-skip-early": case_cutscene_skip_early,
    "boot-continue": case_boot_continue,
    "pad-late": case_pad_late,
    "touch-pad": case_touch_pad,
    "menu-touch": case_menu_touch,
    "menu-pad": case_menu_pad,
    "touch-menu": case_touch_menu,
    "menu-model": case_menu_model,
    "options-back": case_options_back,
    "touch-team": case_touch_team,
    "touch-shop": case_touch_shop,
    "touch-codex": case_touch_codex,
    "touch-worldmap": case_touch_worldmap,
    "touch-stash": case_touch_stash,
    "touch-review": case_touch_review,
    "touch-region": case_touch_region,
    "touch-danger-room": case_touch_danger_room,
    "touch-roster": case_touch_roster,
    "touch-team-stats": case_touch_team_stats,
    "touch-team-skills": case_touch_team_skills,
    "touch-team-gear": case_touch_team_gear,
    "touch-team-ai": case_touch_team_ai,
    "touch-team-assign": case_touch_team_assign,
    "touch-team-skill-details": case_touch_team_skill_details,
    "extract-keyboard-back": case_extract_keyboard_back,
    "stick-travel": case_stick_travel,
    "pad-after-load": case_pad_after_load,
    "pad-persisted": case_pad_persisted,
    "manual-continue": case_manual_continue,
    "deadzone-render": case_deadzone_render,
    "deadzone-water": case_deadzone_water,
    "selector-dialog-800": case_selector_dialog,
    "selector-dialog-720": case_selector_dialog,
    "selector-dialog-4k": case_selector_dialog,
    "extraction-free": case_extraction_free,
    "extraction-off": case_extraction_off,
    "extraction-paid": case_extraction_paid,
}

try:
    from live_visible import case_live_resolution, case_mouse_click
except ImportError as exc:
    refuse("visible live cases could not be loaded: %s" % exc)

CASES.update({
    "live-resolution": case_live_resolution,
    "mouse-click": case_mouse_click,
})


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("case", nargs="?", choices=sorted(CASES))
    ap.add_argument("--port", type=int, default=DEFAULT_PORT)
    ap.add_argument("--binary", type=Path, default=BINARY,
                    help="which x2native build to run (A/B against an older "
                         "binary)")
    ap.add_argument("--set", action="append", default=[], metavar="KEY=VALUE",
                    dest="settings",
                    help="extra runtime cvar, repeatable (e.g. --set "
                         "jit.profile=65536). Passed to x2native as --set, "
                         "which outranks the conf file and the environment.")
    ap.add_argument("--boot-continue", action="store_true",
                    help="pad-persisted: boot through Continue first")
    ap.add_argument("--pacing", choices=("paced", "uncapped", "fast"),
                    default="fast",
                    help="fast = X2_UNPACED+unbounded scheduler; uncapped "
                         "= no guest frame cap only; paced = retail pacing")
    args = ap.parse_args()
    if not args.case:
        ap.print_help()
        return 2
    for setting in args.settings:
        if "=" not in setting:
            refuse("--set wants KEY=VALUE, got %r" % setting)
    if not args.binary.is_file():
        refuse("%s does not exist" % args.binary)
    options = RunOptions(binary=args.binary, pacing=args.pacing,
                         extra_settings=list(args.settings),
                         boot_continue=args.boot_continue)
    case = Case(args.case, args.port, options)
    try:
        CASES[args.case](case)
    except BaseException:
        case.shutdown()
        raise
    return case.finish()


if __name__ == "__main__":
    sys.exit(main())
