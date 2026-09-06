#!/usr/bin/env python3
"""Compare native HUD geometry constants with the player's stored IGB root boxes."""

from __future__ import annotations

import argparse
from pathlib import Path
import re
import sys
from typing import Callable

from shared_dir import shared_dir

ROOT = Path(__file__).resolve().parents[1]
HEADER = ROOT / "src/presentation/hud_geometry.h"
Bound = tuple[tuple[float, float, float], tuple[float, float, float]]
FRAME_ASSETS = {
    "UI/hud/m_healthpanel_pc.IGB": "HUD_HEALTH_FRAME",
    "UI/hud/m_playercross_pc.IGB": "HUD_CROSS",
}
AXES = {"X": 0, "Z": 2}
CONSTANT_NAMES = {
    f"{prefix}_{edge}_{axis}"
    for prefix in FRAME_ASSETS.values()
    for edge in ("MIN", "MAX")
    for axis in AXES
} | {"HUD_PORTRAIT_EXTENT"}


def parse_constants(source: str) -> dict[str, float]:
    """Read the single authoritative numeric definitions without evaluating C."""
    values = {}
    for name, expression in re.findall(r"^#define\s+(HUD_\w+)\s+([^\n]+)$", source, re.M):
        if name not in CONSTANT_NAMES:
            continue
        match = re.fullmatch(r"\((-?\d+(?:\.\d+)?)f\)", expression.strip())
        if not match or name in values:
            raise ValueError(f"HUD geometry: invalid or duplicate definition {name}")
        values[name] = float(match[1])
    missing = CONSTANT_NAMES - values.keys()
    if missing:
        raise ValueError(f"HUD geometry: missing header constants: {', '.join(sorted(missing))}")
    return values


def reader_factory() -> Callable[[Path], Bound]:
    """Load the existing shared format reader without its optional scene importer."""
    checkout = Path(shared_dir("alchemy", "vendor/igblib/igb_format/igb_reader.py"))
    sys.path.insert(0, str(checkout / "vendor"))
    from igblib.igb_format.igb_reader import IGBReader

    def read_bounds(path: Path) -> Bound:
        reader = IGBReader(str(path))
        reader.read()
        scenes = reader.get_objects_by_type(b"igSceneInfo")
        if len(scenes) != 1:
            raise ValueError(f"{path.name}: expected one scene, scanned {len(scenes)}")
        offset = reader.slot_offset
        root = reader.resolve_ref(scenes[0].get_slot(5 + offset))
        if root is None:
            raise ValueError(f"{path.name}: scene root is missing")
        box = reader.resolve_ref(root.get_slot(3 + offset, -1))
        if box is None or not box.is_type(b"igAABox"):
            raise ValueError(f"{path.name}: scene root has no igAABox")
        result = (box.get_slot(2 + offset), box.get_slot(3 + offset))
        if any(not isinstance(edge, tuple) or len(edge) != 3 for edge in result):
            raise ValueError(f"{path.name}: root box has invalid extent fields")
        return result

    return read_bounds


def check_bounds(asset: str, bounds: Bound, constants: dict[str, float],
                 prefix: str | None) -> int:
    """Refuse any mismatch, naming the asset and actual measured value."""
    checked = 0
    for edge_index, edge_name in enumerate(("MIN", "MAX")):
        for axis_name, axis_index in AXES.items():
            name = f"{prefix}_{edge_name}_{axis_name}" if prefix else "HUD_PORTRAIT_EXTENT"
            expected = constants[name]
            if prefix is None and edge_index == 0:
                expected = -expected
            measured = bounds[edge_index][axis_index]
            if measured != expected:
                raise ValueError(
                    f"{asset}: {edge_name}_{axis_name} measured {measured!r}, "
                    f"header {name} requires {expected!r}; checked {checked}/4 fields"
                )
            checked += 1
    return checked


def verify_install(game_dir: Path, constants: dict[str, float],
                   read_bounds: Callable[[Path], Bound]) -> tuple[int, int]:
    if not game_dir.is_dir():
        raise ValueError(f"HUD geometry: install directory missing: {game_dir}; scanned 0 assets")
    portraits = sorted((game_dir / "HUD").glob("hud_head_*.IGB"))
    if not portraits:
        raise ValueError("HUD geometry: scanned HUD/hud_head_*.IGB, matched 0; no portrait evidence")
    candidates = [(game_dir / relative, prefix) for relative, prefix in FRAME_ASSETS.items()]
    candidates.extend((path, None) for path in portraits)
    checked = 0
    for path, prefix in candidates:
        if not path.is_file():
            raise ValueError(f"HUD geometry: missing asset {path}; checked {checked} fields")
        checked += check_bounds(str(path.relative_to(game_dir)), read_bounds(path), constants, prefix)
    return len(candidates), checked


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--game-dir", type=Path, required=True,
                        help="complete player-supplied PC install")
    args = parser.parse_args()
    constants = parse_constants(HEADER.read_text())
    assets, fields = verify_install(args.game_dir, constants, reader_factory())
    print(f"HUD geometry: {assets}/{assets} assets, {assets - len(FRAME_ASSETS)} portraits, "
          f"{fields}/{fields} measured X/Z bounds match {HEADER.relative_to(ROOT)}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
