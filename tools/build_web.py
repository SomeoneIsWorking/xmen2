#!/usr/bin/env python3
"""Build the browser product from the same native owners and guest JIT."""

from __future__ import annotations

import argparse
import os
from pathlib import Path
import shutil
import subprocess
import sys

from resvg_py import svg_to_bytes

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT))
import bootstrap  # noqa: E402

try:
    from .shared_dir import shared_dir
except ImportError:
    from shared_dir import shared_dir

def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--emsdk", type=Path, default=os.environ.get("EMSDK"))
    parser.add_argument("--sdl-source", type=Path)
    parser.add_argument("--skip-deps", action="store_true")
    parser.add_argument("--configure-only", action="store_true")
    parser.add_argument("--jobs", type=int, default=2)
    args = parser.parse_args()
    root = ROOT
    emcmake = (
        args.emsdk / "upstream/emscripten/emcmake"
        if args.emsdk
        else Path(shutil.which("emcmake") or "")
    )
    if not emcmake.is_file():
        parser.error("Emscripten is missing; set EMSDK or pass --emsdk.")
    build = root / "build/web"
    temporary = root / "scratch/web-tool-tmp"
    temporary.mkdir(parents=True, exist_ok=True)
    bootstrap.ensure_shared()
    environment = dict(os.environ, TMPDIR=str(temporary))
    owner = Path(shared_dir("web-port", "tools/web_port.py"))
    prefix = owner / "build/prefix"
    dependencies = [sys.executable, str(owner / "tools/web_port.py"),
                    "--emsdk", str(emcmake.parent.parent.parent),
                    "--jobs", str(args.jobs)]
    if args.sdl_source:
        dependencies.extend(["--sdl-source", str(args.sdl_source.resolve())])
    if args.skip_deps:
        dependencies.append("--check")
    subprocess.run(dependencies, cwd=root, env=environment, check=True)
    command = [
        str(emcmake), "cmake", "-S", str(root), "-B", str(build), "-G", "Ninja",
        "-DCMAKE_BUILD_TYPE=Release", f"-DPython3_EXECUTABLE={sys.executable}",
        "-DCMAKE_EXPORT_COMPILE_COMMANDS=ON",
        "-DCMAKE_C_FLAGS=-pthread",
        "-DCMAKE_CXX_FLAGS=-pthread",
        f"-DX2_WEB_PORT_PREFIX={prefix}", f"-DX2_WEB_PORT_SOURCE={owner}",
        f"-DCMAKE_PREFIX_PATH={prefix}",
        f"-DCMAKE_FIND_ROOT_PATH={prefix}",
        f"-DZLIB_INCLUDE_DIR={prefix / 'include'}",
        f"-DZLIB_LIBRARY={prefix / 'lib/libzlibstatic.a'}",
        "-DCMAKE_DISABLE_FIND_PACKAGE_PkgConfig=ON", "-DZYAN_NO_LIBC=ON",
    ]
    subprocess.run(command, cwd=root, env=environment, check=True)
    if not args.configure_only:
        subprocess.run(
            ["cmake", "--build", str(build), "--target", "x2native", "-j", str(args.jobs)],
            cwd=root, env=environment, check=True,
        )
        package = [sys.executable, str(owner / "tools/package.py"),
                   "--destination", str(root / "build/release/web"),
                   "--lucent", (build / "web-runtime-path.txt").read_text().strip()]
        for name in ("x2native.js", "x2native.wasm", "x2native.data"):
            package.extend(["--file", f"{name}={build / name}"])
        for name in ("index.html", "app.mjs", "style.css", "manifest.webmanifest"):
            package.extend(["--file", f"{name}={root / 'web' / name}"])
        for size in (192, 512):
            icon = build / f"icon-{size}.png"
            icon.write_bytes(svg_to_bytes(svg_path=str(root / "packaging/xmen2-port.svg"),
                                          width=size, height=size))
            package.extend(["--file", f"{icon.name}={icon}"])
        subprocess.run(package, cwd=root, env=environment, check=True)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
