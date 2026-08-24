#!/usr/bin/env python3
"""Build/reuse the derived native asset pack required by the live target."""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import sys
import tempfile


ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))

# The manifest is the ONE authority on how many glyphs there are; a second
# number written down here drifts from it the first time the set changes.
from pad_glyph_manifest import (ICONS, keycap_svg_path,           # noqa: E402
                                svg_paths)
from make_port_pause_menu import write_derived_pause_menu        # noqa: E402

SCRATCH = ROOT / "scratch"
FONT_IGB = ("Textures", "fonts", "x2f_med_pc.igb")
FONT_XMLB = ("UI", "fonts", "x2f_med_pc.xmlb")
PAUSE_IGB = ("UI", "menus", "pause.IGB")
PAUSE_MENUS = (
    ("UI", "menus", "pause.XMLB"),
    ("UI", "menus", "pause.engb"),
    ("UI", "menus", "pause_dr.XMLB"),
    ("UI", "menus", "pause_dr_training.XMLB"),
)
PAUSE_OUTPUTS = tuple(
    Path("ui") / "menus" / parts[-1].lower() for parts in PAUSE_MENUS
)
NATIVE_OUTPUTS = (
    Path("textures/fonts/x2f_med_pc.igb"),
    Path("ui/fonts/x2f_med_pc.xmlb"),
    *PAUSE_OUTPUTS,
)


def case_path(root: Path, parts: tuple[str, ...]) -> Path:
    here = root
    for wanted in parts:
        if not here.is_dir():
            raise SystemExit(f"REFUSING: {here} is not a directory; found 0 {wanted!r} entries")
        found = [p for p in here.iterdir() if p.name.lower() == wanted.lower()]
        if len(found) != 1:
            raise SystemExit(
                f"REFUSING: {here} has {len(found)} case-insensitive matches for {wanted!r}; needs 1"
            )
        here = found[0]
    return here


def fingerprint(paths: list[Path]) -> str:
    digest = hashlib.sha256()
    for path in sorted(paths, key=lambda p: str(p)):
        data = path.read_bytes()
        digest.update(str(path.relative_to(ROOT) if path.is_relative_to(ROOT) else path.name).encode())
        digest.update(len(data).to_bytes(8, "little"))
        digest.update(data)
    return digest.hexdigest()


def output_digests(root: Path) -> dict[str, str]:
    return {
        output.as_posix(): hashlib.sha256((root / output).read_bytes()).hexdigest()
        for output in NATIVE_OUTPUTS
    }


def cached_outputs_match(root: Path, expected: object) -> bool:
    if not isinstance(expected, dict):
        return False
    for output in NATIVE_OUTPUTS:
        path = root / output
        if not path.is_file():
            return False
        if expected.get(output.as_posix()) != hashlib.sha256(path.read_bytes()).hexdigest():
            return False
    return True


def cleanup_tree(path: Path) -> None:
    """Remove exactly one generated tree, refusing anything outside scratch."""
    scratch = SCRATCH.resolve()
    target = path.resolve()
    if target == scratch or scratch not in target.parents:
        raise RuntimeError(f"REFUSING cleanup outside project scratch: {target}")
    if not path.exists():
        return
    for child in sorted(path.rglob("*"), key=lambda p: len(p.parts), reverse=True):
        child.unlink() if child.is_file() or child.is_symlink() else child.rmdir()
    path.rmdir()


def prepare(game: Path, out: Path) -> None:
    igb = case_path(game, FONT_IGB)
    xmlb = case_path(game, FONT_XMLB)
    pause_igb = case_path(game, PAUSE_IGB)
    pause_menus = [case_path(game, parts) for parts in PAUSE_MENUS]
    # The art lives in the SHARED port-assets set, so the fingerprint has to
    # follow it there: a pack cached against the old drawing of a glyph is
    # exactly the stale-vendored-copy failure the shared repo exists to end.
    icons = svg_paths()
    if len(icons) != len(ICONS):
        raise SystemExit(f"REFUSING: the manifest names {len(ICONS)} glyph(s) "
                         f"but resolved {len(icons)} SVG path(s)")
    sources = [igb, xmlb, pause_igb, *pause_menus,
               ROOT / "tools" / "make_pad_font.py",
               ROOT / "tools" / "make_port_pause_menu.py",
               ROOT / "tools" / "pad_glyph_manifest.py",
               ROOT / "pyproject.toml", ROOT / "uv.lock",
               ROOT / "assets" / "buttons" / "glyphs.json",
               keycap_svg_path(), *icons]
    key = fingerprint(sources)
    manifest = out / ".x2-prompt-font.json"
    if manifest.is_file():
        try:
            old = json.loads(manifest.read_text())
        except (OSError, json.JSONDecodeError):
            old = {}
        if old.get("sha256") == key and cached_outputs_match(
                out, old.get("outputs")):
            print(f"native assets: cache HIT {out} ({key[:12]})")
            return

    raw = SCRATCH / "raw"
    raw.mkdir(parents=True, exist_ok=True)
    stage = Path(tempfile.mkdtemp(prefix="native-assets-", dir=raw))
    try:
        result = subprocess.run(
            [sys.executable, str(ROOT / "tools" / "make_pad_font.py"),
             str(igb), str(xmlb), str(stage)], text=True,
            stdout=subprocess.PIPE, stderr=subprocess.STDOUT
        )
        print(result.stdout, end="")
        if result.returncode:
            raise SystemExit(f"native assets: font builder failed with exit {result.returncode}")
        for source, output in zip(pause_menus, PAUSE_OUTPUTS, strict=True):
            write_derived_pause_menu(
                source, pause_igb, stage / output,
            )
        (stage / ".x2-prompt-font.json").write_text(
            json.dumps({"sha256": key, "inputs": len(sources),
                        "outputs": output_digests(stage)}, indent=2) + "\n"
        )
        cleanup_tree(out)
        out.parent.mkdir(parents=True, exist_ok=True)
        stage.rename(out)
        print(f"native assets: cache MISS built {out} ({key[:12]})")
    finally:
        cleanup_tree(stage)


def selftest() -> int:
    base = SCRATCH / "raw" / "prepare-native-assets-selftest"
    cleanup_tree(base)
    (base / "a" / "b").mkdir(parents=True)
    (base / "a" / "b" / "member").write_text("x")
    cleanup_tree(base)
    outside_refused = False
    try:
        cleanup_tree(ROOT)
    except RuntimeError:
        outside_refused = True

    for index, output in enumerate(NATIVE_OUTPUTS):
        path = base / output
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(f"output-{index}".encode())
    digests = output_digests(base)
    complete = cached_outputs_match(base, digests)
    missing = base / PAUSE_OUTPUTS[0]
    missing.unlink()
    missing_refused = not cached_outputs_match(base, digests)
    missing.write_bytes(b"corrupt")
    corrupt_refused = not cached_outputs_match(base, digests)
    cleanup_tree(base)
    passed = (not base.exists() and outside_refused and complete and
              missing_refused and corrupt_refused)
    print("prepare_native_assets selftest: "
          f"cleanup={not base.exists()} outside-refusal={outside_refused} "
          f"complete-cache={complete} missing-refusal={missing_refused} "
          f"corrupt-refusal={corrupt_refused}")
    return 0 if passed else 1


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--selftest", action="store_true")
    parser.add_argument("game", nargs="?", type=Path)
    parser.add_argument("out", nargs="?", type=Path)
    args = parser.parse_args()
    if args.selftest:
        return selftest()
    if not args.game or not args.out:
        parser.error("game and out are required; prepared 0 asset packs")
    prepare(args.game.resolve(), args.out.resolve())
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
