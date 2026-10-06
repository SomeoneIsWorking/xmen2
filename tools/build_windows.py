#!/usr/bin/env python3
"""Configure and build the Windows x86-64 native tree with llvm-mingw."""

from __future__ import annotations

import argparse
import os
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))

import ci  # noqa: E402
import windows_deps  # noqa: E402

DEFAULT_BUILD = ROOT / "build" / "windows-x86_64"


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-dir", type=Path, default=DEFAULT_BUILD)
    parser.add_argument("--deps-root", type=Path, default=windows_deps.DEFAULT_DEPS_ROOT)
    parser.add_argument("--build-type", default="RelWithDebInfo")
    parser.add_argument("--jobs", type=int, default=os.cpu_count() or 2)
    parser.add_argument("--target", action="append", dest="targets",
                        help="build only these targets (default: all)")
    return parser.parse_args()


def environment(deps: windows_deps.WindowsDependencies,
                base: dict[str, str]) -> dict[str, str]:
    result = dict(base)
    # FFmpeg is described by the prefix's own pkg-config files and nothing else.
    result["PKG_CONFIG_LIBDIR"] = str(deps.prefix / "lib" / "pkgconfig")
    result.pop("PKG_CONFIG_PATH", None)
    return result


def configure_command(build: Path, build_type: str,
                      deps: windows_deps.WindowsDependencies) -> list[str]:
    return ["cmake", "-S", str(ROOT), "-B", str(build), "-G", "Ninja",
            f"-DCMAKE_TOOLCHAIN_FILE={windows_deps.TOOLCHAIN_FILE}",
            f"-DLLVM_MINGW_ROOT={deps.llvm_mingw}",
            f"-DCMAKE_FIND_ROOT_PATH={deps.prefix.as_posix()}",
            f"-DCMAKE_PREFIX_PATH={deps.prefix.as_posix()}",
            f"-DZLIB_LIBRARY={deps.zlib_library.as_posix()}",
            f"-DCMAKE_BUILD_TYPE={build_type}",
            "-DCMAKE_EXPORT_COMPILE_COMMANDS=ON",
            f"-DPython3_EXECUTABLE={sys.executable}"]


def run(command: list[str], env: dict[str, str]) -> None:
    print("+", " ".join(command), flush=True)
    result = subprocess.run(command, cwd=ROOT, env=env)
    if result.returncode:
        raise SystemExit(f"build_windows: exit {result.returncode}: {' '.join(command)}")


def main() -> int:
    args = parse_args()
    base = dict(os.environ)
    ci.ensure_shared(base)
    deps = windows_deps.provision(args.deps_root, args.jobs)
    env = environment(deps, base)
    build = args.build_dir.resolve()
    run(configure_command(build, args.build_type, deps), env)
    command = ["cmake", "--build", str(build), "--parallel", str(args.jobs)]
    if args.targets:
        command += ["--target", *args.targets]
    run(command, env)
    print(f"build_windows: built {build}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
