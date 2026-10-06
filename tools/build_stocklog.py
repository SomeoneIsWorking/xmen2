#!/usr/bin/env python3
"""Build and stage the stock-game D3D8 logging proxy.

The staged directory is a symlink farm over the read-only game install.  The
only copied/replaced files are alchemy.ini, d3d8.dll, and d3d8_real.dll.

Usage:
    tools/build_stocklog.py [rundir-name]       # default: stocklog
    X2_KEYS="..." tools/run_shim.py stocklog 540
    X2_SHADOW_FORCE=0 X2_SHADOW_EXPECT=0 tools/run_shim.py stocklog 540
"""

from __future__ import annotations

import argparse
import os
from pathlib import Path
import shlex
import shutil
import subprocess
import sys
from typing import Mapping, Sequence


TOOL_NAME = "build_stocklog"
COMPILER = "i686-w64-mingw32-gcc"
PROXY_SOURCES = (
    "tools/proxy_d3d8/proxy.c",
    "tools/proxy_d3d8/fwd.S",
    "tools/proxy_d3d8/shadow_setting.c",
    "tools/proxy_d3d8/shadow_trace.c",
)
OWNED_RUN_FILES = frozenset(
    {
        "alchemy.ini",
        "d3d8.dll",
        "d3d8_real.dll",
        "d3d8_lightlog.txt",
        "d3d8_shadow_trace.jsonl",
    }
)


class BuildRefusal(RuntimeError):
    """An expected precondition failed; building would produce bad evidence."""


def _read_env(path: Path) -> dict[str, str]:
    """Read the simple KEY=VALUE format used by the tracked .env.example."""
    values: dict[str, str] = {}
    if not path.is_file():
        return values

    for line_number, raw_line in enumerate(path.read_text().splitlines(), 1):
        line = raw_line.strip()
        if not line or line.startswith("#"):
            continue
        if line.startswith("export "):
            line = line[7:].lstrip()
        if "=" not in line:
            raise BuildRefusal(f"{path}:{line_number}: expected KEY=VALUE")
        key, raw_value = line.split("=", 1)
        key = key.strip()
        if not key or not key.replace("_", "a").isalnum() or key[0].isdigit():
            raise BuildRefusal(f"{path}:{line_number}: invalid variable name {key!r}")
        try:
            words = shlex.split(raw_value, comments=True, posix=True)
        except ValueError as exc:
            raise BuildRefusal(f"{path}:{line_number}: {exc}") from exc
        if len(words) > 1:
            raise BuildRefusal(f"{path}:{line_number}: quote values containing whitespace")
        values[key] = words[0] if words else ""
    return values


def load_environment(root: Path, inherited: Mapping[str, str]) -> dict[str, str]:
    """Match the old script: inherited environment first, then .env overrides."""
    result = dict(inherited)
    result.update(_read_env(root / ".env"))
    return result


def _rooted_path(root: Path, value: str) -> Path:
    path = Path(value).expanduser()
    return path if path.is_absolute() else root / path


def validate_run_name(run_name: str) -> Path:
    """Return a safe relative path below scratch/run, refusing traversal."""
    candidate = Path(run_name)
    if (
        not run_name
        or candidate.is_absolute()
        or candidate.name in {"", ".", ".."}
        or len(candidate.parts) != 1
    ):
        raise BuildRefusal(f"run directory name {run_name!r} must be one name below scratch/run")
    return candidate


def configured_paths(root: Path, run_name: str, env: Mapping[str, str]) -> dict[str, Path]:
    game_text = env.get("GAME_PC_DIR", "")
    if not game_text:
        raise BuildRefusal("set GAME_PC_DIR in .env (see .env.example)")
    wine_text = env.get("WINEPREFIX", "") or env.get("WINE_PREFIX", "")
    if not wine_text:
        raise BuildRefusal("set WINE_PREFIX in .env")

    game_dir = _rooted_path(root, game_text)
    if not game_dir.is_dir():
        raise BuildRefusal(f"GAME_PC_DIR={game_dir} is not a directory")

    relative_run = validate_run_name(run_name)
    wine_dir = _rooted_path(root, wine_text)
    build_dir = root / "build/proxy"
    return {
        "game": game_dir,
        "real": wine_dir / "drive_c/windows/syswow64/d3d8.dll",
        "run": root / "scratch/run" / relative_run,
        "build": build_dir,
        "definition": build_dir / "d3d8.def",
        "output": build_dir / "d3d8.dll",
    }


def run_checked(
    command: Sequence[str],
    *,
    capture: bool = False,
    quiet: bool = False,
    cwd: Path | None = None,
) -> str:
    stdout = subprocess.PIPE if capture or quiet else None
    stderr = subprocess.PIPE if capture else (subprocess.DEVNULL if quiet else None)
    completed = subprocess.run(
        list(command),
        check=False,
        text=True,
        stdout=stdout,
        stderr=stderr,
        cwd=cwd,
    )
    if completed.returncode != 0:
        if capture and completed.stderr:
            sys.stderr.write(completed.stderr)
        raise BuildRefusal(f"command failed ({completed.returncode}): {shlex.join(command)}")
    return completed.stdout or ""


def pe_output(root: Path, mode: str, image: Path, *extra: str) -> str:
    return run_checked(
        [sys.executable, str(root / "tools/pe.py"), mode, str(image), *extra],
        capture=True,
        cwd=root,
    )


def export_names(output: str) -> list[str]:
    names: list[str] = []
    for line in output.splitlines():
        if not line.startswith(" "):
            continue
        fields = line.split()
        if fields:
            names.append(fields[-1])
    return names


def ordinal_d3d8_import_count(output: str) -> int:
    count = 0
    for line in output.splitlines():
        fields = line.split()
        if len(fields) >= 2 and fields[0].lower() == "d3d8.dll" and fields[1].startswith("@"):
            count += 1
    return count


def rewrite_proxy_definition(generated: str) -> tuple[str, int]:
    """Replace exactly Direct3DCreate8's forwarder with our decorated symbol."""
    old = '  "Direct3DCreate8" = "d3d8_real.Direct3DCreate8"'
    new = "  Direct3DCreate8 = Direct3DCreate8@4"
    lines = generated.splitlines()
    own_count = sum(line == old for line in lines)
    total_exports = sum(line.startswith('  "') for line in lines)
    if own_count != 1:
        raise BuildRefusal(
            "the generated .def was not rewritten as expected -- "
            f"{own_count} Direct3DCreate8 forwarder(s), want 1. "
            "pe.py proxydef's output format changed. Built NOTHING."
        )
    rewritten = "\n".join(new if line == old else line for line in lines) + "\n"
    forward_count = sum('= "d3d8_real.' in line for line in rewritten.splitlines())
    if forward_count != total_exports - 1:
        raise BuildRefusal(
            "the generated .def was not rewritten as expected -- "
            f"1 local Direct3DCreate8, {forward_count} forwarder(s) of "
            f"{total_exports} exports. pe.py proxydef's output format changed. "
            "Built NOTHING."
        )
    return rewritten, forward_count


def compile_command(root: Path, output: Path, definition: Path) -> list[str]:
    return [
        COMPILER,
        "-shared",
        "-O2",
        "-Wall",
        "-Wextra",
        "-o",
        str(output),
        f"-I{root / 'src/oracle'}",
        *(str(root / source) for source in PROXY_SOURCES),
        str(definition),
        "-static-libgcc",
    ]


def require_sources(root: Path) -> None:
    missing = [str(root / source) for source in PROXY_SOURCES if not (root / source).is_file()]
    if missing:
        raise BuildRefusal("proxy source(s) missing; built NOTHING:\n  " + "\n  ".join(missing))


def _replace_with_symlink(source: Path, destination: Path) -> None:
    if destination.is_symlink() or destination.is_file():
        destination.unlink()
    elif destination.exists():
        raise BuildRefusal(f"refusing to replace directory in staged run: {destination}")
    destination.symlink_to(source)


def _prepare_owned_file(path: Path) -> None:
    if path.is_symlink() or path.is_file():
        path.unlink()
    elif path.exists():
        raise BuildRefusal(f"refusing to replace directory at owned path: {path}")


def stage_run(
    game_dir: Path,
    run_dir: Path,
    real_d3d8: Path,
    proxy: Path,
) -> None:
    run_dir.mkdir(parents=True, exist_ok=True)
    for source in game_dir.iterdir():
        if source.name in OWNED_RUN_FILES:
            continue
        _replace_with_symlink(source, run_dir / source.name)

    alchemy = game_dir / "alchemy.ini"
    if not alchemy.is_file():
        raise BuildRefusal(f"{alchemy} does not exist; staged NOTHING")
    contents = alchemy.read_text()
    _prepare_owned_file(run_dir / "alchemy.ini")
    (run_dir / "alchemy.ini").write_text(
        contents.replace("multiSampleType = 4", "multiSampleType = 0")
    )
    _prepare_owned_file(run_dir / "d3d8_real.dll")
    _prepare_owned_file(run_dir / "d3d8.dll")
    shutil.copy2(real_d3d8, run_dir / "d3d8_real.dll")
    shutil.copy2(proxy, run_dir / "d3d8.dll")

    stale_log = run_dir / "d3d8_lightlog.txt"
    _prepare_owned_file(stale_log)
    stale_shadow_trace = run_dir / "d3d8_shadow_trace.jsonl"
    _prepare_owned_file(stale_shadow_trace)


def build(
    root: Path,
    run_name: str,
    inherited_env: Mapping[str, str],
) -> Path:
    env = load_environment(root, inherited_env)
    paths = configured_paths(root, run_name, env)

    if shutil.which(COMPILER) is None:
        raise BuildRefusal(
            f"{COMPILER} not found -- built NOTHING "
            "(ask the user to run: sudo dnf install mingw32-gcc)"
        )

    real_d3d8 = paths["real"]
    if not real_d3d8.is_file():
        raise BuildRefusal(
            f"no 32-bit d3d8 at {real_d3d8}.\n"
            "  This prefix has no DXVK d3d8; the stock game cannot start without\n"
            "  one (run_shim.py forces d3d8=native). Built NOTHING."
        )

    real_exports = pe_output(root, "exports", real_d3d8)
    if "Direct3DCreate8" not in export_names(real_exports):
        raise BuildRefusal(
            f"{real_d3d8} has no Direct3DCreate8 export -- that is not a\n"
            "  d3d8 implementation. Built NOTHING."
        )

    gfx_dll = paths["game"] / "libIGGfx.dll"
    imports = pe_output(root, "imports", gfx_dll)
    ordinal_count = ordinal_d3d8_import_count(imports)
    if ordinal_count:
        raise BuildRefusal(
            f"libIGGfx imports {ordinal_count} symbol(s) from d3d8 BY ORDINAL;\n"
            "  the generated .def forwards names only. Built NOTHING."
        )

    paths["build"].mkdir(parents=True, exist_ok=True)
    generated_def = pe_output(root, "proxydef", real_d3d8, "d3d8_real")
    rewritten_def, forward_count = rewrite_proxy_definition(generated_def)
    paths["definition"].write_text(rewritten_def)
    print(
        f"{TOOL_NAME}: {forward_count} export(s) forwarded to d3d8_real, "
        "Direct3DCreate8 implemented"
    )

    require_sources(root)
    try:
        run_checked(compile_command(root, paths["output"], paths["definition"]), cwd=root)
    except BuildRefusal as exc:
        raise BuildRefusal("compile FAILED") from exc

    built_exports = pe_output(root, "exports", paths["output"])
    built_names = export_names(built_exports)
    if "Direct3DCreate8" not in built_names:
        raise BuildRefusal(f"the BUILT {paths['output']} does not export Direct3DCreate8.")
    real_count = len(export_names(real_exports))
    if len(built_names) < real_count:
        raise BuildRefusal(
            f"the proxy exports {len(built_names)} symbol(s); the real d3d8\n"
            f"  exports {real_count}. A missing export is a load failure later."
        )

    stage_run(paths["game"], paths["run"], real_d3d8, paths["output"])
    print(f"{TOOL_NAME}: staged {paths['run']}")
    print(f"  d3d8.dll      = the logging proxy ({paths['output'].stat().st_size} bytes)")
    print(f"  d3d8_real.dll = {real_d3d8}")
    print(f"  run it:  X2_KEYS=... tools/run_shim.py {run_name} 540")
    print(f"  read it: scratch/run/{run_name}/d3d8_lightlog.txt")
    print(
        "  shadow: set X2_SHADOW_FORCE=0|1 and X2_SHADOW_EXPECT=0|1, press "
        "F9 in the selected scene, then read "
        f"scratch/run/{run_name}/d3d8_shadow_trace.jsonl"
    )
    return paths["run"]


def parse_args(argv: Sequence[str] | None = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("run_name", nargs="?", default="stocklog")
    return parser.parse_args(argv)


def main(argv: Sequence[str] | None = None) -> int:
    args = parse_args(argv)
    root = Path(__file__).resolve().parent.parent
    try:
        build(root, args.run_name, os.environ)
    except BuildRefusal as exc:
        print(f"{TOOL_NAME}: {exc}", file=sys.stderr)
        return 2
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
