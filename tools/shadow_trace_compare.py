#!/usr/bin/env python3
"""Compare one bounded stock DetailedShadow-off capture with one on capture.

Each run uses independent X2_SHADOW_FORCE and X2_SHADOW_EXPECT inputs. The
proxy validates the executable's RegQueryValueExA import and substitutes only
the successful DetailedShadow DWORD query, then records the original, forced,
observed and expected values. F9 arms exactly the next D3D8 frame. This tool
refuses swapped settings, incomplete captures, event overflow, and captures
that reached no draw. It compares only behavior observable at the D3D8
boundary; title-internal guest-function probes are deliberately not required
tooling.
"""

import argparse
import json
import pathlib
import sys


class Refuse(Exception):
    pass


RESOURCE_KEYS = (
    "create_texture",
    "get_surface_level",
    "create_render_target",
    "create_depth_stencil",
    "copy_rects",
    "update_texture",
    "default_render_target",
    "default_depth_stencil",
)


def validate_summary(summary: dict, source: object) -> dict:
    if summary.get("dropped_events"):
        raise Refuse(
            f"{source} dropped {summary['dropped_events']} event(s); raise "
            "X2_SHADOW_MAX_EVENTS and capture again"
        )
    if summary.get("dropped_resource_events"):
        raise Refuse(
            f"{source} dropped {summary['dropped_resource_events']} resource "
            "event(s); raise X2_SHADOW_MAX_EVENTS and capture again"
        )
    if summary.get("texture_hook_failures"):
        raise Refuse(
            f"{source} had {summary['texture_hook_failures']} texture vtable "
            "hook failure(s); GetSurfaceLevel coverage is incomplete"
        )
    return summary


def validate_control(control: dict, summary: dict, source: object) -> None:
    if control.get("seam") != "RegQueryValueExA:DetailedShadow":
        raise Refuse(f"{source} used unrecognized control seam {control.get('seam')!r}")
    if not isinstance(control.get("forced_reads"), int) or control["forced_reads"] < 1:
        raise Refuse(f"{source} did not intercept a successful DetailedShadow DWORD query")
    if control.get("forced") != control.get("observed"):
        raise Refuse(
            f"{source} forced {control.get('forced')!r}, but read back {control.get('observed')!r}"
        )
    if control.get("observed") != summary.get("detailed_shadow"):
        raise Refuse(
            f"{source} control observed {control.get('observed')!r}, but the "
            f"selected frame retained {summary.get('detailed_shadow')!r}"
        )
    if control.get("expected") != summary.get("detailed_shadow"):
        raise Refuse(
            f"{source} independently expected {control.get('expected')!r}, "
            f"but the selected frame retained "
            f"{summary.get('detailed_shadow')!r}"
        )


def read_summary(path: pathlib.Path) -> dict:
    if not path.is_file():
        raise Refuse(f"{path} does not exist; compared nothing")
    summaries = []
    controls = []
    for number, line in enumerate(path.read_text().splitlines(), 1):
        try:
            record = json.loads(line)
        except json.JSONDecodeError as exc:
            raise Refuse(f"{path}:{number}: invalid JSON: {exc}") from exc
        if record.get("event") == "summary":
            summaries.append(record)
        if record.get("event") == "control":
            controls.append(record)
    if len(summaries) != 1:
        raise Refuse(
            f"{path} contains {len(summaries)} summary record(s), expected "
            "exactly one F9-selected frame"
        )
    if len(controls) != 1:
        raise Refuse(
            f"{path} contains {len(controls)} control record(s), expected "
            "exactly one validated registry-query intervention"
        )
    summary = summaries[0]
    validate_control(controls[0], summary, path)
    return validate_summary(summary, path)


def compare(off: dict, on: dict) -> str:
    if off.get("detailed_shadow") != 0:
        raise Refuse("the off capture did not observe DetailedShadow=0 in XMen2.exe")
    if on.get("detailed_shadow") != 1:
        raise Refuse("the on capture did not observe DetailedShadow=1 in XMen2.exe")
    if int(off.get("draws", 0)) < 1 or int(on.get("draws", 0)) < 1:
        raise Refuse("one compared frame reached no D3D8 draw")

    route_parts = []
    for scope in ("frame_resources", "total_resources"):
        off_resources = off.get(scope, {})
        on_resources = on.get(scope, {})
        changes = []
        for key in RESOURCE_KEYS:
            before = int(off_resources.get(key, 0))
            after = int(on_resources.get(key, 0))
            if before != after:
                changes.append(f"{key} {before}->{after}")
        label = "frame" if scope == "frame_resources" else "capture-total"
        route_parts.append(f"{label} " + (", ".join(changes) if changes else "no count change"))
    routes = "; ".join(route_parts)
    return (
        "DetailedShadow control: "
        f"draws {int(off.get('draws', 0))}->{int(on.get('draws', 0))}; "
        f"resource route: {routes}"
    )


def selftest() -> None:
    off = {
        "detailed_shadow": 0,
        "draws": 3,
        "dropped_events": 0,
        "frame_resources": {key: 0 for key in RESOURCE_KEYS},
        "total_resources": {key: 0 for key in RESOURCE_KEYS},
    }
    on = {
        "detailed_shadow": 1,
        "draws": 5,
        "dropped_events": 0,
        "frame_resources": {
            **{key: 0 for key in RESOURCE_KEYS},
            "create_texture": 1,
            "get_surface_level": 1,
        },
        "total_resources": {
            **{key: 0 for key in RESOURCE_KEYS},
            "create_texture": 2,
        },
    }
    result = compare(off, on)
    assert "draws 3->5" in result
    assert "create_texture 0->1" in result

    for broken, needle in (
        (({**off, "detailed_shadow": 1}, on), "off capture"),
        ((off, {**on, "detailed_shadow": 0}), "on capture"),
        ((off, {**on, "draws": 0}), "no D3D8 draw"),
    ):
        try:
            compare(*broken)
        except Refuse as exc:
            assert needle in str(exc)
        else:
            raise AssertionError(f"expected refusal containing {needle!r}")
    try:
        validate_summary({**on, "texture_hook_failures": 1}, "fixture")
    except Refuse as exc:
        assert "coverage is incomplete" in str(exc)
    else:
        raise AssertionError("expected texture-hook coverage refusal")
    for control, needle in (
        (
            {
                "seam": "RegQueryValueExA:DetailedShadow",
                "forced_reads": 1,
                "forced": 0,
                "observed": 1,
                "expected": 1,
            },
            "read back",
        ),
        (
            {
                "seam": "RegQueryValueExA:DetailedShadow",
                "forced_reads": 1,
                "forced": 1,
                "observed": 1,
                "expected": 0,
            },
            "independently expected",
        ),
        (
            {
                "seam": "RegQueryValueExA:DetailedShadow",
                "forced_reads": 0,
                "forced": 1,
                "observed": 1,
                "expected": 1,
            },
            "did not intercept",
        ),
        (
            {
                "seam": "XMen2.exe+0x668d40",
                "forced_reads": 1,
                "forced": 1,
                "observed": 1,
                "expected": 1,
            },
            "unrecognized",
        ),
    ):
        try:
            validate_control(control, on, "fixture")
        except Refuse as exc:
            assert needle in str(exc)
        else:
            raise AssertionError(f"expected control refusal containing {needle!r}")
    print("shadow_trace_compare: D3D8 boundary delta and eight refusals proved")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("off", nargs="?", type=pathlib.Path)
    parser.add_argument("on", nargs="?", type=pathlib.Path)
    parser.add_argument("--selftest", action="store_true")
    args = parser.parse_args()
    if args.selftest:
        selftest()
        return 0
    if args.off is None or args.on is None:
        parser.error("OFF and ON trace paths are required unless --selftest is used")
    try:
        print(compare(read_summary(args.off), read_summary(args.on)))
    except Refuse as exc:
        print(f"shadow_trace_compare: REFUSED: {exc}", file=sys.stderr)
        return 2
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
