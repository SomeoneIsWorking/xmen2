#!/usr/bin/env python3
"""Build/reuse the derived pause-menu asset pack required by the live target."""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import sys
import tempfile


ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))

from make_port_pause_menu import (RESERVED_NODE_COPIES,           # noqa: E402
                                  _synthetic_menu,
                                  write_derived_pause_menu)

SCRATCH = ROOT / "scratch"
BUILD = ROOT / "build"
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
NATIVE_OUTPUTS = PAUSE_OUTPUTS


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
    """Remove one generated tree below the project scratch or build root."""
    roots = (SCRATCH.resolve(), BUILD.resolve())
    target = path.resolve()
    if not any(target != root and root in target.parents for root in roots):
        raise RuntimeError(f"REFUSING cleanup outside project scratch/build roots: {target}")
    if not path.exists():
        return
    for child in sorted(path.rglob("*"), key=lambda p: len(p.parts), reverse=True):
        child.unlink() if child.is_file() or child.is_symlink() else child.rmdir()
    path.rmdir()


def prepare(game: Path, out: Path) -> None:
    pause_igb = case_path(game, PAUSE_IGB)
    pause_menus = [case_path(game, parts) for parts in PAUSE_MENUS]
    sources = [pause_igb, *pause_menus,
               ROOT / "tools" / "make_port_pause_menu.py",
               ROOT / "pyproject.toml", ROOT / "uv.lock"]
    key = fingerprint(sources)
    manifest = out / ".x2-native-assets.json"
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
        for source, output in zip(pause_menus, PAUSE_OUTPUTS, strict=True):
            write_derived_pause_menu(
                source, pause_igb, stage / output,
            )
        (stage / ".x2-native-assets.json").write_text(
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
    game = base / "game"
    out = base / "out"
    (game / "UI" / "menus").mkdir(parents=True)
    (game / "UI" / "menus" / "pause.IGB").write_bytes(
        b"\x00button10\x00" * RESERVED_NODE_COPIES
    )
    for parts in PAUSE_MENUS:
        path = game.joinpath(*parts)
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(_synthetic_menu())

    outside_refused = False
    try:
        cleanup_tree(ROOT)
    except RuntimeError:
        outside_refused = True

    captured_inputs: list[Path] = []
    real_fingerprint = fingerprint

    def recording_fingerprint(paths: list[Path]) -> str:
        captured_inputs.extend(paths)
        return real_fingerprint(paths)

    globals()["fingerprint"] = recording_fingerprint
    try:
        prepare(game, out)
    finally:
        globals()["fingerprint"] = real_fingerprint

    expected_outputs = {path.as_posix() for path in NATIVE_OUTPUTS}
    actual_outputs = {
        path.relative_to(out).as_posix()
        for path in out.rglob("*")
        if path.is_file() and path.name != ".x2-native-assets.json"
    }
    complete = expected_outputs == actual_outputs
    forbidden_inputs = [
        path for path in captured_inputs
        if "font" in path.as_posix().lower()
        or "glyph" in path.as_posix().lower()
        or path.suffix.lower() == ".svg"
    ]
    forbidden_outputs = [
        path for path in out.rglob("*")
        if "font" in path.as_posix().lower()
        or "glyph" in path.as_posix().lower()
        or path.suffix.lower() == ".svg"
    ]
    manifest = json.loads((out / ".x2-native-assets.json").read_text())
    inputs_exact = manifest.get("inputs") == len(captured_inputs)
    missing = out / PAUSE_OUTPUTS[0]
    missing.unlink()
    missing_refused = not cached_outputs_match(out, manifest.get("outputs"))
    prepare(game, out)
    restored = cached_outputs_match(
        out, json.loads((out / ".x2-native-assets.json").read_text()).get("outputs")
    )
    cleanup_tree(base)
    passed = (not base.exists() and outside_refused and complete and
              inputs_exact and not forbidden_inputs and not forbidden_outputs and
              missing_refused and restored)
    print("prepare_native_assets selftest: "
          f"cleanup={not base.exists()} outside-refusal={outside_refused} "
          f"real-prepare={complete} pause-inputs-only={not forbidden_inputs} "
          f"pause-outputs-only={not forbidden_outputs} "
          f"input-accounting={inputs_exact} missing-refusal={missing_refused} "
          f"rebuilt={restored}")
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
