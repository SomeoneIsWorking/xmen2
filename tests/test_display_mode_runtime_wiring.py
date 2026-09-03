#!/usr/bin/env python3
"""Pin the evidenced retail resolution-load override and its ordering."""

from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
from check_save_trace_wiring import retail_body_call


ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "src/native/display_mode_runtime.c"


def require(source: str, needle: str) -> int:
    position = source.find(needle)
    if position < 0:
        raise SystemExit(f"display_mode_runtime wiring missing: {needle}")
    return position


def main() -> int:
    source = SOURCE.read_text(encoding="utf-8")
    retained = require(source, retail_body_call(0x00619770) + ";")
    publish = require(source, "(void)x2_display_mode_seed_publish();")
    reread = require(source, "reread_resolution(C, exe, expected);")

    if not retained < publish < reread:
        raise SystemExit(
            "display_mode_runtime must retain retail first-run initialization "
            "before republishing and rereading Resolution"
        )
    for needle in (
        "LINKED_SETTINGS_LOAD = 0x00619770u",
        "RVA_BUILD_REGISTRY_CONTEXT = 0x00216a70u",
        "RVA_READ_STRING = 0x00216e10u",
        "RVA_PUBLISHER_NAME = 0x002a3a64u",
        "RVA_PRODUCT_NAME = 0x002a3a70u",
        "RVA_RESOLUTION_PATH = 0x002a4dccu",
        "RVA_RESOLUTION_DEFAULT = 0x002a4e40u",
        "RVA_RESOLUTION_OUTPUT = 0x00668d9cu",
        "x2_display_mode_seed_format(settings->width, settings->height,",
        "x86_guest_call_args(&call, exe + RVA_BUILD_REGISTRY_CONTEXT, 8u);",
        "call.esp = context;",
        "call.ecx = context;",
        "x86_guest_call_args(&call, exe + RVA_READ_STRING, 16u);",
        'x86_register_override("XMen2.exe", LINKED_SETTINGS_LOAD,',
    ):
        require(source, needle)

    print("display_mode_runtime wiring: retained body -> publish -> retail reread")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
