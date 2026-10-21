#!/usr/bin/env python3
"""Provision the Linux x86-64 release dependency prefix.

SDL3, SDL3_image, FreeType and FFmpeg are built from the pinned archives in
`dependency_archives.py` into one static prefix, and glslc from pinned shaderc
sources, all with the host's CC/CXX. A release binary linked against this
prefix needs only the C/C++ runtime and the libraries SDL loads at run time
from the host, so its glibc floor is the build host's.
"""

from __future__ import annotations

import argparse
from dataclasses import dataclass
import json
import os
from pathlib import Path
import shutil
import sys

from dependency_archives import (
    FFMPEG, FFMPEG_COMMON_OPTIONS, FFMPEG_LIBRARIES, FREETYPE, FREETYPE_OPTIONS, SDL3,
    SDL3_IMAGE, SDL3_IMAGE_OPTIONS, SDL3_OPTIONS, Archive, fetch, github_commit, refuse, run,
    unpack)


ROOT = Path(__file__).resolve().parents[1]
DEFAULT_DEPS_ROOT = ROOT / "build" / "deps" / "linux"

CMAKE_PACKAGES = (
    (SDL3, SDL3_OPTIONS),
    (SDL3_IMAGE, SDL3_IMAGE_OPTIONS),
    (FREETYPE, FREETYPE_OPTIONS),
)

# shaderc and the third_party trees it builds glslc from.
SHADERC = github_commit("SomeoneIsWorking", "shaderc", "50f71a748725b3df267128e519ef6c59881fc33e",
                        "b269580b3df0220173925f313369eb735dc30787f52b1a2d9fa60e1dfc776bbf")
SHADERC_THIRD_PARTY = (
    ("glslang", github_commit("KhronosGroup", "glslang",
                              "f0bd0257c308b9a26562c1a30c4748a0219cc951",
                              "bd58dca4dac67dcf7640292d7d63e0416274d40ee2200f7301878cec11ac6647")),
    ("spirv-tools", github_commit("KhronosGroup", "SPIRV-Tools",
                                  "fbe4f3ad913c44fe8700545f8ffe35d1382b7093",
                                  "cabb35f4eef0da3ef72ad9edd596af4191d7507a8f35c05df526d2d5ff889f59")),
    ("spirv-headers", github_commit("KhronosGroup", "SPIRV-Headers",
                                    "04f10f650d514df88b76d25e83db360142c7b174",
                                    "1b220e3eec1714f0451b0e3652979bd280edf10893f617837b88e6359a804ded")),
)


@dataclass(frozen=True)
class Toolchain:
    cc: str
    cxx: str
    jobs: int


def cmake_build(source: Path, build: Path, prefix: Path, options: tuple[str, ...],
                tools: Toolchain) -> None:
    if build.exists():
        shutil.rmtree(build)
    run(["cmake", "-S", str(source), "-B", str(build), "-G", "Ninja",
         f"-DCMAKE_C_COMPILER={tools.cc}", f"-DCMAKE_CXX_COMPILER={tools.cxx}",
         f"-DCMAKE_PREFIX_PATH={prefix}", "-DCMAKE_BUILD_TYPE=Release",
         "-DCMAKE_POSITION_INDEPENDENT_CODE=ON", "-DCMAKE_INSTALL_LIBDIR=lib",
         f"-DCMAKE_INSTALL_PREFIX={prefix}", *options])
    run(["cmake", "--build", str(build), "--parallel", str(tools.jobs)])
    run(["cmake", "--install", str(build)])


def cmake_package(archive: Archive, options: tuple[str, ...], prefix: Path, work: Path,
                  tools: Toolchain) -> None:
    source = unpack(fetch(archive, work.parent / "sources"), work / "src")
    cmake_build(source, work / "build" / archive.name, prefix, options, tools)


def ffmpeg_package(prefix: Path, work: Path, tools: Toolchain) -> None:
    source = unpack(fetch(FFMPEG, work.parent / "sources"), work / "src")
    build = work / "build" / FFMPEG.name
    if build.exists():
        shutil.rmtree(build)
    build.mkdir(parents=True)
    run([str(source / "configure"), f"--prefix={prefix}", f"--cc={tools.cc}", f"--cxx={tools.cxx}",
         "--enable-pic", *FFMPEG_COMMON_OPTIONS], cwd=build)
    run(["make", f"-j{tools.jobs}"], cwd=build)
    run(["make", "install"], cwd=build)


def glslc_package(prefix: Path, work: Path, tools: Toolchain) -> None:
    sources = work.parent / "sources"
    tree = unpack(fetch(SHADERC, sources), work / "src")
    for directory, archive in SHADERC_THIRD_PARTY:
        destination = tree / "third_party" / directory
        if not destination.is_dir():
            unpack(fetch(archive, sources), work / "src").rename(destination)
    stage = work / "shaderc-install"
    cmake_build(tree, work / "build" / "shaderc", stage,
                ("-DSHADERC_SKIP_TESTS=ON", "-DSHADERC_SKIP_EXAMPLES=ON",
                 "-DSHADERC_SKIP_COPYRIGHT_CHECK=ON", "-DSHADERC_ENABLE_WGSL_OUTPUT=OFF",
                 "-DSKIP_GLSLANG_INSTALL=ON", "-DSKIP_SPIRV_TOOLS_INSTALL=ON",
                 "-DSPIRV_SKIP_TESTS=ON", "-DSPIRV_SKIP_EXECUTABLES=ON",
                 "-DENABLE_GLSLANG_BINARIES=OFF", f"-DPython3_EXECUTABLE={sys.executable}"),
                tools)
    (prefix / "bin").mkdir(parents=True, exist_ok=True)
    shutil.copy2(stage / "bin" / "glslc", prefix / "bin" / "glslc")


def contract(tools: Toolchain) -> str:
    return json.dumps({
        "compilers": [tools.cc, tools.cxx],
        "archives": {archive.name: archive.sha256
                     for archive in (*(pkg for pkg, _ in CMAKE_PACKAGES), FFMPEG, SHADERC,
                                     *(dep for _, dep in SHADERC_THIRD_PARTY))},
        "cmake_options": {pkg.name: list(options) for pkg, options in CMAKE_PACKAGES},
        "ffmpeg": list(FFMPEG_COMMON_OPTIONS),
    }, indent=2, sort_keys=True) + "\n"


def required_files(prefix: Path) -> list[Path]:
    return [prefix / "lib/cmake/SDL3/SDL3Config.cmake",
            prefix / "lib/cmake/SDL3_image/SDL3_imageConfig.cmake",
            prefix / "lib/cmake/freetype/freetype-config.cmake",
            *(prefix / f"lib/lib{name}.a" for name in FFMPEG_LIBRARIES),
            prefix / "lib/pkgconfig/libavcodec.pc",
            prefix / "bin/glslc"]


def provision(deps_root: Path, tools: Toolchain) -> Path:
    """Return the prefix, building it unless its marker matches this contract."""
    deps_root = deps_root.resolve()
    prefix = deps_root / "x86_64"
    marker = prefix / "linux-dependencies.json"
    wanted = contract(tools)
    if (marker.is_file() and marker.read_text(encoding="utf-8") == wanted
            and all(path.is_file() for path in required_files(prefix))):
        return prefix
    if prefix.exists():
        shutil.rmtree(prefix)
    work = deps_root / "work"
    if work.exists():
        shutil.rmtree(work)
    for archive, options in CMAKE_PACKAGES:
        cmake_package(archive, options, prefix, work, tools)
    ffmpeg_package(prefix, work, tools)
    glslc_package(prefix, work, tools)
    missing = [str(path) for path in required_files(prefix) if not path.is_file()]
    if missing:
        refuse("the prefix build did not install " + ", ".join(missing))
    marker.write_text(wanted, encoding="utf-8")
    shutil.rmtree(work)
    return prefix


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--deps-root", type=Path, default=DEFAULT_DEPS_ROOT)
    parser.add_argument("--jobs", type=int, default=os.cpu_count() or 2)
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    cc = os.environ.get("CC")
    cxx = os.environ.get("CXX")
    if not cc or not cxx:
        refuse("set CC and CXX to the release compiler (clang and clang++)")
    print(f"linux_deps: prefix {provision(args.deps_root, Toolchain(cc, cxx, args.jobs))}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
