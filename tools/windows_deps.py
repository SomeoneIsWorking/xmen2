#!/usr/bin/env python3
"""Provision the Windows x86-64 toolchain and native dependency prefix.

llvm-mingw and every library come from an official release archive pinned by
SHA-256 here; the libraries are built from source with that toolchain into one
static prefix. Nothing comes from the host's package manager.
"""

from __future__ import annotations

import argparse
from dataclasses import dataclass
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import platform
import shutil
import subprocess
import sys
import tarfile
import urllib.request
import zipfile


ROOT = Path(__file__).resolve().parents[1]
DEFAULT_DEPS_ROOT = ROOT / "build" / "deps" / "windows"
TOOLCHAIN_FILE = ROOT / "cmake" / "toolchains" / "llvm-mingw-x86_64.cmake"
TRIPLE = "x86_64-w64-mingw32"


@dataclass(frozen=True)
class Archive:
    name: str
    version: str
    url: str
    sha256: str

    @property
    def filename(self) -> str:
        suffix = next(ending for ending in (".tar.gz", ".tar.xz", ".zip")
                      if self.url.endswith(ending))
        return f"{self.name}-{self.version}{suffix}"


LLVM_MINGW_VERSION = "20260922"
LLVM_MINGW = {
    ("Linux", "x86_64"): Archive(
        "llvm-mingw", f"{LLVM_MINGW_VERSION}-ucrt-ubuntu-22.04-x86_64",
        "https://github.com/mstorsjo/llvm-mingw/releases/download/"
        f"{LLVM_MINGW_VERSION}/llvm-mingw-{LLVM_MINGW_VERSION}-ucrt-ubuntu-22.04-x86_64.tar.xz",
        "bb7bb7654b33d5aa8712acb837c963b2e0c56352560c76105270a3268c665c21"),
    ("Windows", "AMD64"): Archive(
        "llvm-mingw", f"{LLVM_MINGW_VERSION}-ucrt-x86_64",
        "https://github.com/mstorsjo/llvm-mingw/releases/download/"
        f"{LLVM_MINGW_VERSION}/llvm-mingw-{LLVM_MINGW_VERSION}-ucrt-x86_64.zip",
        "e3ad77d117a4bea19a7a3b333341824d79a5a371004a10e25b8504e7b3047666"),
}

ZLIB = Archive(
    "zlib", "1.3.2",
    "https://github.com/madler/zlib/releases/download/v1.3.2/zlib-1.3.2.tar.gz",
    "bb329a0a2cd0274d05519d61c667c062e06990d72e125ee2dfa8de64f0119d16")
SDL3 = Archive(
    "SDL3", "3.4.16",
    "https://github.com/libsdl-org/SDL/releases/download/release-3.4.16/SDL3-3.4.16.tar.gz",
    "7322236cd12090c3eb40b9728be4d49c76f66ad17d04369584d4ecad5cf77c68")
SDL3_IMAGE = Archive(
    "SDL3_image", "3.4.6",
    "https://github.com/libsdl-org/SDL_image/releases/download/release-3.4.6/"
    "SDL3_image-3.4.6.tar.gz",
    "d2e4637ae700f72e5196b8fbd749850ed2e5e1e09c5a5be8d06ff55aaccf3b01")
FREETYPE = Archive(
    "freetype", "2.14.1",
    "https://github.com/freetype/freetype/archive/refs/tags/VER-2-14-1.tar.gz",
    "44bd69d1f0750603410cfd4f26f1c5523c5a3a087a2dfa8639b757dc7c6677be")
FFMPEG = Archive(
    "ffmpeg", "n7.1.1",
    "https://github.com/FFmpeg/FFmpeg/archive/refs/tags/n7.1.1.tar.gz",
    "f117507dc501f2a6c11f9241d8d0c3213846cfad91764361af37befd6b6c523d")

CMAKE_PACKAGES = (
    (ZLIB, ("-DZLIB_BUILD_SHARED=OFF", "-DZLIB_BUILD_STATIC=ON", "-DZLIB_BUILD_TESTING=OFF")),
    (SDL3, (
        "-DSDL_SHARED=OFF", "-DSDL_STATIC=ON", "-DSDL_TESTS=OFF",
        "-DSDL_TEST_LIBRARY=OFF", "-DSDL_EXAMPLES=OFF", "-DSDL_INSTALL=ON")),
    (SDL3_IMAGE, (
        "-DBUILD_SHARED_LIBS=OFF", "-DSDLIMAGE_VENDORED=OFF", "-DSDLIMAGE_DEPS_SHARED=OFF",
        "-DSDLIMAGE_BACKEND_STB=ON", "-DSDLIMAGE_BACKEND_WIC=OFF", "-DSDLIMAGE_SAMPLES=OFF",
        "-DSDLIMAGE_TESTS=OFF", "-DSDLIMAGE_PNG_LIBPNG=OFF", "-DSDLIMAGE_AVIF=OFF", "-DSDLIMAGE_JXL=OFF",
        "-DSDLIMAGE_TIF=OFF", "-DSDLIMAGE_WEBP=OFF", "-DSDLIMAGE_INSTALL=ON")),
    (FREETYPE, (
        "-DBUILD_SHARED_LIBS=OFF", "-DFT_DISABLE_BZIP2=ON", "-DFT_DISABLE_BROTLI=ON",
        "-DFT_DISABLE_HARFBUZZ=ON", "-DFT_DISABLE_PNG=ON", "-DFT_DISABLE_ZLIB=ON")),
)

# The container, codecs and parser the title's SFD movies and WMA streams use.
FFMPEG_FEATURES = (
    "--enable-demuxer=mpegps,asf",
    "--enable-decoder=mpeg1video,adpcm_adx,wmav1,wmav2,wmapro,wmavoice",
    "--enable-parser=mpegvideo",
    "--enable-protocol=file",
)
FFMPEG_LIBRARIES = ("avformat", "avcodec", "swscale", "swresample", "avutil")


@dataclass(frozen=True)
class WindowsDependencies:
    llvm_mingw: Path
    prefix: Path


def refuse(message: str) -> None:
    raise SystemExit(f"windows_deps: {message}")


def run(command: list[str], cwd: Path | None = None) -> None:
    print("+", " ".join(command), flush=True)
    result = subprocess.run(command, cwd=cwd)
    if result.returncode:
        refuse(f"exit {result.returncode}: {' '.join(command)}")


def sha256_of(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for block in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def fetch(archive: Archive, cache: Path) -> Path:
    cache.mkdir(parents=True, exist_ok=True)
    path = cache / archive.filename
    if not path.is_file():
        partial = path.with_name(path.name + ".part")
        print(f"windows_deps: downloading {archive.url}", flush=True)
        with urllib.request.urlopen(archive.url) as response, partial.open("wb") as out:
            shutil.copyfileobj(response, out)
        partial.replace(path)
    actual = sha256_of(path)
    if actual != archive.sha256:
        refuse(f"{path} has SHA-256 {actual}, expected {archive.sha256}; "
               "delete it to download again")
    return path


def single_root(names: list[str], source: Path) -> str:
    roots = {PurePosixPath(name).parts[0] for name in names if name.strip("./")}
    if len(roots) != 1:
        refuse(f"{source} must hold one top-level directory, found {sorted(roots)}")
    return roots.pop()


def unpack(path: Path, destination: Path) -> Path:
    """Extract an archive's one top-level directory under `destination`."""
    destination.mkdir(parents=True, exist_ok=True)
    if path.name.endswith(".zip"):
        with zipfile.ZipFile(path) as bundle:
            names = bundle.namelist()
            tree = destination / single_root(names, path)
            if not tree.is_dir():
                base = destination.resolve()
                for name in names:
                    target = (destination / name).resolve()
                    if target != base and base not in target.parents:
                        refuse(f"{path} member escapes the destination: {name}")
                bundle.extractall(destination)
    else:
        with tarfile.open(path) as bundle:
            names = bundle.getnames()
            tree = destination / single_root(names, path)
            if not tree.is_dir():
                bundle.extractall(destination, filter="data")
    return tree


def host_key() -> tuple[str, str]:
    return platform.system(), platform.machine()


def llvm_mingw_root(deps_root: Path) -> Path:
    """Use LLVM_MINGW_DIR when it is set, else the pinned release."""
    selected = os.environ.get("LLVM_MINGW_DIR")
    if selected:
        root = Path(selected).expanduser().resolve()
    else:
        archive = LLVM_MINGW.get(host_key())
        if archive is None:
            refuse(f"no pinned llvm-mingw release for host {host_key()}; "
                   "set LLVM_MINGW_DIR to an llvm-mingw directory")
        root = unpack(fetch(archive, deps_root / "sources"), deps_root)
    compiler = root / "bin" / f"{TRIPLE}-clang{'.exe' if os.name == 'nt' else ''}"
    if not compiler.is_file():
        refuse(f"{root} is not an llvm-mingw toolchain: {compiler} is missing")
    return root


def cmake_package(archive: Archive, options: tuple[str, ...], toolchain: Path,
                  prefix: Path, work: Path, jobs: int) -> None:
    source = unpack(fetch(archive, work.parent / "sources"), work / "src")
    build = work / "build" / archive.name
    if build.exists():
        shutil.rmtree(build)
    run(["cmake", "-S", str(source), "-B", str(build), "-G", "Ninja",
         f"-DCMAKE_TOOLCHAIN_FILE={TOOLCHAIN_FILE}", f"-DLLVM_MINGW_ROOT={toolchain}",
         f"-DCMAKE_FIND_ROOT_PATH={prefix.as_posix()}", f"-DCMAKE_PREFIX_PATH={prefix.as_posix()}",
         "-DCMAKE_BUILD_TYPE=Release", "-DCMAKE_INSTALL_LIBDIR=lib",
         f"-DCMAKE_INSTALL_PREFIX={prefix.as_posix()}", *options])
    run(["cmake", "--build", str(build), "--parallel", str(jobs)])
    run(["cmake", "--install", str(build)])


def ffmpeg_package(toolchain: Path, prefix: Path, work: Path, jobs: int) -> None:
    shell = shutil.which("sh")
    make = shutil.which("make")
    if not shell or not make:
        refuse("FFmpeg's configure needs a POSIX sh and make; on Windows put "
               "MSYS2's usr/bin on PATH after `pacman -S make`")
    source = unpack(fetch(FFMPEG, work.parent / "sources"), work / "src")
    build = work / "build" / FFMPEG.name
    if build.exists():
        shutil.rmtree(build)
    build.mkdir(parents=True)
    # configure runs under a POSIX sh, which reads a backslash as an escape;
    # MSYS2's sh takes C:/... paths and finds the .exe itself.
    def tool(name: str) -> str:
        return (toolchain / "bin" / name).as_posix()

    run([shell, (source / "configure").as_posix(),
         f"--prefix={prefix.as_posix()}", "--target-os=mingw32", "--arch=x86_64",
         "--enable-cross-compile", f"--cc={tool(TRIPLE + '-clang')}",
         f"--cxx={tool(TRIPLE + '-clang++')}", f"--ar={tool('llvm-ar')}",
         f"--ranlib={tool('llvm-ranlib')}", f"--nm={tool('llvm-nm')}",
         f"--strip={tool('llvm-strip')}", f"--windres={tool(TRIPLE + '-windres')}",
         "--enable-static", "--disable-shared", "--disable-programs", "--disable-doc",
         "--disable-debug", "--disable-network", "--disable-avdevice",
         "--disable-avfilter", "--disable-everything", "--disable-autodetect",
         "--disable-iconv", "--disable-zlib", "--disable-x86asm",
         *FFMPEG_FEATURES, "--enable-swscale", "--enable-swresample"],
        cwd=build)
    run([make, f"-j{jobs}"], cwd=build)
    run([make, "install"], cwd=build)


def contract(toolchain: Path) -> str:
    return json.dumps({
        "llvm_mingw": str(toolchain),
        "archives": {archive.name: archive.sha256
                     for archive in (*(pkg for pkg, _ in CMAKE_PACKAGES), FFMPEG)},
        "cmake_options": {pkg.name: list(options) for pkg, options in CMAKE_PACKAGES},
        "ffmpeg": list(FFMPEG_FEATURES),
        "toolchain_file": sha256_of(TOOLCHAIN_FILE),
    }, indent=2, sort_keys=True) + "\n"


def required_files(prefix: Path) -> list[Path]:
    return [prefix / "include/zlib.h",
            prefix / "lib/cmake/SDL3/SDL3Config.cmake",
            prefix / "lib/cmake/SDL3_image/SDL3_imageConfig.cmake",
            prefix / "lib/cmake/freetype/freetype-config.cmake",
            *(prefix / f"lib/lib{name}.a" for name in FFMPEG_LIBRARIES),
            prefix / "lib/pkgconfig/libavcodec.pc"]


def provision(deps_root: Path, jobs: int) -> WindowsDependencies:
    deps_root = deps_root.resolve()
    toolchain = llvm_mingw_root(deps_root)
    prefix = deps_root / "x86_64"
    marker = prefix / "windows-dependencies.json"
    wanted = contract(toolchain)
    if (marker.is_file() and marker.read_text(encoding="utf-8") == wanted
            and all(path.is_file() for path in required_files(prefix))):
        return WindowsDependencies(toolchain, prefix)
    if prefix.exists():
        shutil.rmtree(prefix)
    work = deps_root / "work"
    for archive, options in CMAKE_PACKAGES:
        cmake_package(archive, options, toolchain, prefix, work, jobs)
    ffmpeg_package(toolchain, prefix, work, jobs)
    missing = [str(path) for path in required_files(prefix) if not path.is_file()]
    if missing:
        refuse("the prefix build did not install " + ", ".join(missing))
    marker.write_text(wanted, encoding="utf-8")
    shutil.rmtree(work)
    return WindowsDependencies(toolchain, prefix)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--deps-root", type=Path, default=DEFAULT_DEPS_ROOT)
    parser.add_argument("--jobs", type=int, default=os.cpu_count() or 2)
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    deps = provision(args.deps_root, args.jobs)
    print(f"windows_deps: llvm-mingw {deps.llvm_mingw}")
    print(f"windows_deps: prefix {deps.prefix}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
