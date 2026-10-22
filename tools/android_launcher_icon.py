#!/usr/bin/env python3
"""Rasterise packaging/xmen2-port.svg into the APK's launcher mipmaps.

    uv run --frozen python tools/android_launcher_icon.py <res-dir>
"""

from __future__ import annotations

import argparse
from pathlib import Path

from resvg_py import svg_to_bytes

ROOT = Path(__file__).resolve().parent.parent
SOURCE = ROOT / "packaging/xmen2-port.svg"
NAME = "xmen2_port_icon.png"
# Legacy launcher icon is 48dp.
DENSITIES = {"mdpi": 48, "hdpi": 72, "xhdpi": 96, "xxhdpi": 144, "xxxhdpi": 192}


def render(res: Path) -> None:
    for density, size in DENSITIES.items():
        target = res / f"mipmap-{density}" / NAME
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(bytes(svg_to_bytes(svg_path=str(SOURCE), width=size, height=size)))


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("res", type=Path)
    render(parser.parse_args().res)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
