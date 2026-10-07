#!/usr/bin/env python3
"""Refuse a touch owner that decides anything from the platform it built for.

Touch is a DEVICE, not a package. Which controls a player gets is answered from
the live event stream (`src/input/touch_source.cpp`) and from one setting, so a
Windows tablet, a Linux 2-in-1 and a phone all reach the same code. The moment
one of these files asks `#ifdef __ANDROID__`, that stops being true for whoever
is not on the platform the branch was written for -- silently, because the other
platform still compiles and still runs, just without the feature.

The check names every file it inspected and refuses when it inspected none: a
pass that scanned zero files is not evidence of anything. Renaming or moving an
owner therefore breaks this loudly rather than quietly reducing its coverage.

An owner added to the codemap is added here in the same change.
"""

from __future__ import annotations

import re
import sys
from pathlib import Path

# The touch owners named by docs/touch-play.md and docs/codemap.md. Adding an
# owner there means adding it here; a file listed and missing is a refusal.
OWNERS = (
    "src/input/touch_source.cpp",
    "src/input/touch_source.h",
    "src/input/touch_controls.cpp",
    "src/input/touch_controls.h",
    "src/input/touch_runtime.cpp",
    "src/input/touch_runtime.h",
    "src/input/touch_census.cpp",
    "src/input/touch_census.h",
    "src/input/touch_menu_controls.cpp",
    "src/input/touch_menu_controls.h",
    "src/input/touch_pad.cpp",
    "src/input/touch_pad.h",
    "src/input/touch_pad_publisher.cpp",
    "src/input/touch_pad_publisher.h",
    "src/input/touch_pointer.cpp",
    "src/input/touch_pointer.h",
    "src/input/touch_inject.cpp",
    "src/input/touch_inject.h",
    "src/input/touch_visuals.cpp",
    "src/input/touch_visuals.h",
    "src/input/touch_skip_button.cpp",
    "src/input/touch_skip_button.h",
    "src/input/thumb_stick.cpp",
    "src/input/thumb_stick.h",
    "src/input/cutscene_skip.cpp",
    "src/input/cutscene_skip.h",
    "src/input/touch_menu.cpp",
    "src/input/touch_menu.hpp",
    "src/input/touch_menu_layout.cpp",
    "src/input/touch_menu_layout.hpp",
    "src/input/touch_menu_view.cpp",
    "src/input/touch_menu_view.hpp",
    "src/input/touch_menu_parts.cpp",
    "src/input/touch_menu_parts.hpp",
    "src/input/touch_menu_team.cpp",
    "src/input/touch_menu_team.hpp",
    "src/input/touch_menu_shop.cpp",
    "src/input/touch_menu_shop.hpp",
    "src/input/touch_menu_codex.cpp",
    "src/input/touch_menu_codex.hpp",
    "src/input/touch_menu_worldmap.cpp",
    "src/input/touch_menu_worldmap.hpp",
    "src/input/touch_menu_review.cpp",
    "src/input/touch_menu_review.hpp",
    "src/input/touch_runtime_menu.hpp",
    # input.touch_controls decides touch play on every platform.
    "src/config/settings.cpp",
    "src/input/gameplay_control.cpp",
    "src/input/gameplay_control.h",
    "src/presentation/touch_layout.cpp",
    "src/presentation/touch_layout.h",
    "src/presentation/hud_layout.cpp",
    "src/presentation/hud_layout.h",
    "src/presentation/retail_scene_plane.cpp",
    "src/presentation/retail_scene_plane.hpp",
    "src/native/touch_hud_runtime.cpp",
    "src/native/touch_hud_runtime.h",
    "src/native/power_slots_runtime.cpp",
    "src/native/power_slots.h",
    "src/native/touch_menu_source.cpp",
    "src/native/touch_menu_source.hpp",
    "src/ui/touch_document.cpp",
    "src/ui/touch_document.hpp",
    "src/ui/skip_document.cpp",
    "src/ui/skip_document.hpp",
    "src/ui/touch_menu_document.cpp",
    "src/ui/touch_menu_document.hpp",
    "src/ui/igb_textures.cpp",
    "src/ui/igb_textures.hpp",
)

# Platform identity, as the preprocessor sees it. `SDL_PLATFORM_*` is SDL's own
# spelling of the same question and is caught for the same reason.
PLATFORM_MACROS = re.compile(
    r"\b(__ANDROID__|ANDROID|__APPLE__|_WIN32|_WIN64|__linux__|"
    r"__EMSCRIPTEN__|TARGET_OS_[A-Z_]+|SDL_PLATFORM_[A-Z0-9_]+)\b"
)
CONDITIONAL = re.compile(r"^\s*#\s*(if|ifdef|ifndef|elif)\b")


def offences(root: Path) -> tuple[list[str], list[str]]:
    """Return (findings, inspected). A missing owner is itself a finding."""
    findings: list[str] = []
    inspected: list[str] = []
    for name in OWNERS:
        path = root / name
        if not path.is_file():
            findings.append(
                f"{name}: listed as a touch owner but not present. Either it "
                f"moved -- update this list and docs/touch-play.md -- or the "
                f"owner was deleted."
            )
            continue
        inspected.append(name)
        for number, line in enumerate(path.read_text(encoding="utf-8").splitlines(), start=1):
            if not CONDITIONAL.match(line):
                continue
            found = PLATFORM_MACROS.search(line)
            if found:
                findings.append(
                    f"{name}:{number}: touch play is compiled per platform "
                    f"here ({found.group(0)}). Ask the device, not the build: "
                    f"x2_touch_source_note() and input.touch_controls own that "
                    f"answer. See docs/touch-play.md."
                )
    return findings, inspected


def selftest(root: Path) -> int:
    findings, inspected = offences(root)
    if findings or not inspected:
        print(
            "check_touch_portable selftest: the tree itself must pass before "
            "the detector can be trusted",
            file=sys.stderr,
        )
        return 1
    probe = re.compile(PLATFORM_MACROS)
    if not probe.search("#ifdef __ANDROID__") or not CONDITIONAL.match("#ifdef __ANDROID__"):
        print(
            "check_touch_portable selftest: the detector does not detect the "
            "thing it exists to detect",
            file=sys.stderr,
        )
        return 1
    if CONDITIONAL.match("  /* __ANDROID__ is not the answer */"):
        print(
            "check_touch_portable selftest: a comment was read as a preprocessor branch",
            file=sys.stderr,
        )
        return 1
    print(
        "check_touch_portable selftest: rejects a platform branch, accepts a comment mentioning one"
    )
    return 0


def main() -> int:
    root = Path(__file__).resolve().parent.parent
    if len(sys.argv) > 1:
        if sys.argv[1] != "--selftest":
            print("usage: check_touch_portable.py [--selftest]", file=sys.stderr)
            return 2
        return selftest(root)
    findings, inspected = offences(root)
    if not inspected:
        print(
            "check_touch_portable: inspected 0 touch owners -- this check "
            "proves nothing. Fix the owner list before trusting a pass.",
            file=sys.stderr,
        )
        return 1
    if findings:
        for finding in findings:
            print(finding, file=sys.stderr)
        return 1
    print(
        f"check_touch_portable: {len(inspected)} of {len(OWNERS)} touch owners "
        f"inspected, 0 platform branches -- touch play is decided by the "
        f"device on every platform"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
