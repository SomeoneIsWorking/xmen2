#!/usr/bin/env python3
"""Resolve the shared touch SVG set for build-time UI staging.

The shared manifest owns completeness and rejects missing or undeclared SVGs.
The required resources below are the title's build contract; RmlUi owns the
action-to-resource mapping. An older shared checkout must fail before packaging.
"""

from pathlib import Path
import sys

from shared_dir import shared_dir

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, shared_dir("port-assets", marker="sets"))
import port_assets  # noqa: E402

REQUIRED = (
    "attack", "smash", "use", "jump", "powers", "pause",
    "power1", "power2", "power3", "power4",
)


def sources() -> list[Path]:
    names = port_assets.names("touch-controls", start=ROOT)
    missing = set(REQUIRED) - set(names)
    if missing:
        raise SystemExit("touch icons: shared touch-controls is missing required UI resources: "
                         + ", ".join(sorted(missing)))
    return [port_assets.path("touch-controls", name, start=ROOT) for name in REQUIRED]


if __name__ == "__main__":
    for source in sources():
        print(source)
