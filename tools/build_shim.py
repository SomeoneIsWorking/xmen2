#!/usr/bin/env python3
"""Build and stage a libIG proxy/trace DLL for stock-game comparison."""

from __future__ import annotations

import argparse
import os
from pathlib import Path
import shutil
import sys
from typing import Mapping, Sequence

try:
    from tools.build_stocklog import BuildRefusal, load_environment, run_checked
except ModuleNotFoundError:
    from build_stocklog import BuildRefusal, load_environment, run_checked


COMPILER = "i686-w64-mingw32-gcc"
MODES = frozenset({"proxy", "trace"})


def validate_target(mode: str, dll_name: str) -> tuple[str, str]:
    if mode not in MODES:
        raise BuildRefusal(f"unknown mode {mode!r}; expected proxy or trace")
    candidate = Path(dll_name)
    if candidate.name != dll_name or candidate.suffix.lower() != ".dll" or not candidate.stem:
        raise BuildRefusal(f"DLL {dll_name!r} must be one .dll file name")
    return candidate.stem, f"{candidate.stem}_orig"


def configured_paths(
    root: Path, mode: str, dll_name: str, env: Mapping[str, str]
) -> dict[str, Path]:
    base, original = validate_target(mode, dll_name)
    game_text = env.get("GAME_PC_DIR", "")
    if not game_text:
        raise BuildRefusal("set GAME_PC_DIR in .env (see .env.example)")
    game_dir = Path(game_text).expanduser()
    if not game_dir.is_absolute():
        game_dir = root / game_dir
    source = game_dir / dll_name
    if not source.is_file():
        raise BuildRefusal(f"no {dll_name} in GAME_PC_DIR={game_dir}")
    build_dir = root / "build" / "shim" / mode / base
    return {
        "game": game_dir,
        "source": source,
        "build": build_dir,
        "output": build_dir / dll_name,
        "definition": build_dir / f"{base}.def",
        "surface": build_dir / "surface.txt",
        "run": root / "scratch" / "run" / mode,
        "original": Path(f"{original}.dll"),
    }


def surface_inputs(game_dir: Path) -> list[Path]:
    inputs = sorted(game_dir.glob("*.exe"))
    inputs.extend(sorted(game_dir.glob("libIG*.dll")))
    movie = game_dir / "libMovie.dll"
    if movie.is_file() and movie not in inputs:
        inputs.append(movie)
    if not inputs:
        raise BuildRefusal(f"{game_dir} contains no executable/import surface")
    return inputs


def build_proxy(root: Path, paths: Mapping[str, Path], original: str) -> None:
    definition = run_checked(
        [
            sys.executable,
            str(root / "tools" / "pe.py"),
            "proxydef",
            str(paths["source"]),
            original,
        ],
        capture=True,
        cwd=root,
    )
    paths["definition"].write_text(definition, encoding="utf-8")
    run_checked(
        [
            COMPILER,
            "-shared",
            "-nostdlib",
            "-o",
            str(paths["output"]),
            str(paths["definition"]),
        ],
        cwd=root,
    )


def build_trace(root: Path, dll_name: str, paths: Mapping[str, Path], original: str) -> None:
    surface = run_checked(
        [
            sys.executable,
            str(root / "tools" / "pe.py"),
            "surface",
            dll_name,
            *(str(path) for path in surface_inputs(paths["game"])),
        ],
        capture=True,
        cwd=root,
    )
    paths["surface"].write_text(surface, encoding="utf-8")
    run_checked(
        [
            sys.executable,
            str(root / "tools" / "gen_trace.py"),
            str(paths["source"]),
            original,
            str(paths["surface"]),
            str(paths["build"]),
        ],
        cwd=root,
    )
    run_checked(
        [
            COMPILER,
            "-shared",
            "-o",
            dll_name,
            "trace.def",
            "trace.c",
            "thunks.S",
            "-Wall",
            "-O2",
            "-static-libgcc",
        ],
        cwd=paths["build"],
    )


def _replace_file(path: Path) -> None:
    if path.is_file() or path.is_symlink():
        path.unlink()
    elif path.exists():
        raise BuildRefusal(f"refusing to replace directory at {path}")


def stage_run(paths: Mapping[str, Path], dll_name: str) -> None:
    run_dir = paths["run"]
    run_dir.mkdir(parents=True, exist_ok=True)
    for source in paths["game"].iterdir():
        if source.name in {dll_name, "alchemy.ini", paths["original"].name}:
            continue
        destination = run_dir / source.name
        _replace_file(destination)
        destination.symlink_to(source)

    alchemy = paths["game"] / "alchemy.ini"
    if not alchemy.is_file():
        raise BuildRefusal(f"{alchemy} does not exist; staged NOTHING")
    for destination in (
        run_dir / dll_name,
        run_dir / paths["original"],
        run_dir / "alchemy.ini",
    ):
        _replace_file(destination)
    shutil.copy2(paths["source"], run_dir / paths["original"])
    shutil.copy2(paths["output"], run_dir / dll_name)
    (run_dir / "alchemy.ini").write_text(
        alchemy.read_text(encoding="utf-8").replace("multiSampleType = 4", "multiSampleType = 0"),
        encoding="utf-8",
    )


def build(root: Path, mode: str, dll_name: str, inherited_env: Mapping[str, str]) -> Path:
    _, original = validate_target(mode, dll_name)
    env = load_environment(root, inherited_env)
    paths = configured_paths(root, mode, dll_name, env)
    if shutil.which(COMPILER) is None:
        raise BuildRefusal(
            f"{COMPILER} not found -- built NOTHING "
            "(ask the user to run: sudo dnf install mingw32-gcc)"
        )
    paths["build"].mkdir(parents=True, exist_ok=True)
    if mode == "proxy":
        build_proxy(root, paths, original)
    else:
        build_trace(root, dll_name, paths, original)
    if not paths["output"].is_file():
        raise BuildRefusal(f"build produced no {dll_name}")
    stage_run(paths, dll_name)
    print(f"build_shim: staged {paths['run']} ({dll_name} -> {original}.dll)")
    print(f"  run it: X2_TRACE=x2trace.log tools/run_shim.py {mode} 60")
    return paths["run"]


def parse_args(argv: Sequence[str] | None = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("mode", choices=sorted(MODES))
    parser.add_argument("dll")
    return parser.parse_args(argv)


def main(argv: Sequence[str] | None = None) -> int:
    args = parse_args(argv)
    root = Path(__file__).resolve().parents[1]
    try:
        build(root, args.mode, args.dll, os.environ)
    except BuildRefusal as exc:
        print(f"build_shim: {exc}", file=sys.stderr)
        return 2
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
