#!/usr/bin/env python3
"""Refuse a touch owner that decides anything from the platform it built for.

Touch is a DEVICE, not a package. Which controls a player gets is answered from
the live event stream (`src/input/touch_source.c`) and from one setting, so a
Windows tablet, a Linux 2-in-1 and a phone all reach the same code. The moment
one of these files asks `#ifdef __ANDROID__`, that stops being true for whoever
is not on the platform the branch was written for -- silently, because the other
platform still compiles and still runs, just without the feature.

The check names every file it inspected and refuses when it inspected none: a
pass that scanned zero files is not evidence of anything. Renaming or moving an
owner therefore breaks this loudly rather than quietly reducing its coverage.
"""

from __future__ import annotations

import re
import sys
from pathlib import Path

# The touch owners named by docs/touch-play.md and docs/codemap.md. Adding an
# owner there means adding it here; a file listed and missing is a refusal.
OWNERS = (
    "src/input/touch_source.c",
    "src/input/touch_source.h",
    "src/input/touch_controls.cpp",
    "src/input/touch_controls.h",
    "src/input/touch_runtime.cpp",
    "src/input/touch_runtime.h",
    "src/input/gameplay_control.c",
    "src/input/gameplay_control.h",
    "src/presentation/touch_layout.c",
    "src/presentation/touch_layout.h",
    "src/native/touch_hud_runtime.c",
    "src/native/touch_hud_runtime.h",
    "src/ui/touch_document.cpp",
    "src/ui/touch_document.hpp",
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
