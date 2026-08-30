#!/usr/bin/env python3
"""Build the small, cross-compiled native dependency set for the APK.

Android must never resolve FFmpeg through the host's pkg-config database. This
tool owns the reproducible source download and produces a prefix that CMake
consumes through X2_ANDROID_FFMPEG_ROOT.
"""

from __future__ import annotations

import argparse
import hashlib
import os
from pathlib import Path, PurePosixPath
import subprocess
import tarfile
import urllib.request


FFMPEG_VERSION = "n7.1.1"
FFMPEG_URL = (
    "https://github.com/FFmpeg/FFmpeg/archive/refs/tags/"
    f"{FFMPEG_VERSION}.tar.gz"
)
FFMPEG_SHA256 = "f117507dc501f2a6c11f9241d8d0c3213846cfad91764361af37befd6b6c523d"
LIBRARIES = ("avformat", "avcodec", "swscale", "swresample", "avutil")


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--prefix", type=Path, default=Path("scratch/android-deps/arm64-v8a"))
    parser.add_argument("--abi", default="arm64-v8a", choices=("arm64-v8a", "x86_64"))
    parser.add_argument("--api", type=int, default=34)
    parser.add_argument("--ndk", type=Path)
    parser.add_argument("--jobs", type=int, default=max(1, min(os.cpu_count() or 1, 4)))
    return parser.parse_args()


def find_ndk(explicit: Path | None) -> Path:
    if explicit:
        return explicit
    sdk = os.environ.get("ANDROID_HOME") or os.environ.get("ANDROID_SDK_ROOT")
    version = os.environ.get("ANDROID_NDK_VERSION")
    if not sdk or not version:
        raise SystemExit(
            "Android NDK is not selected. Set ANDROID_HOME and "
            "ANDROID_NDK_VERSION, or pass --ndk <path>."
        )
    return Path(sdk) / "ndk" / version


def archive(cache: Path) -> Path:
    cache.mkdir(parents=True, exist_ok=True)
    downloaded = cache / f"ffmpeg-{FFMPEG_VERSION}.tar.gz"
    if not downloaded.exists():
        print(f"Downloading FFmpeg {FFMPEG_VERSION} into {downloaded}")
        urllib.request.urlretrieve(FFMPEG_URL, downloaded)
    digest = hashlib.sha256(downloaded.read_bytes()).hexdigest()
    if digest != FFMPEG_SHA256:
        raise SystemExit(
            f"FFmpeg archive checksum mismatch: expected {FFMPEG_SHA256}, got {digest}"
        )
    return downloaded


def archive_root(members: list[tarfile.TarInfo]) -> str:
    """The archive's single top-level directory, read rather than guessed.

    FFmpeg's GitHub tarball roots at "FFmpeg-n7.1.1" while the download URL
    spells the same version "ffmpeg-n7.1.1", so a name composed from the
    version never matched on a case-sensitive filesystem: the tree was
    re-extracted on every run and then refused as missing.
    """
    roots = {PurePosixPath(member.name).parts[0] for member in members if member.name.strip("./")}
    if len(roots) != 1:
        raise SystemExit(
            f"FFmpeg archive must hold exactly one top-level directory, found {sorted(roots)}"
        )
    return roots.pop()


def source_tree(cache: Path) -> Path:
    downloaded = archive(cache)
    with tarfile.open(downloaded, "r:gz") as bundle:
        members = bundle.getmembers()
        source = cache / archive_root(members)
        if source.is_dir() and (source / "configure").is_file():
            return source
        print(f"Extracting {downloaded} into {cache}")
        destination = cache.resolve()
        for member in members:
            if member.issym() or member.islnk():
                raise SystemExit(f"Refusing linked FFmpeg archive member: {member.name}")
            target = (cache / member.name).resolve()
            if target != destination and destination not in target.parents:
                raise SystemExit(f"Refusing unsafe FFmpeg archive member: {member.name}")
            bundle.extract(member, cache)
    if not (source / "configure").is_file():
        raise SystemExit(f"FFmpeg archive did not unpack a source tree at {source}")
    return source


def target_triple(abi: str) -> str:
    return {"arm64-v8a": "aarch64-linux-android", "x86_64": "x86_64-linux-android"}[abi]


def build(args: argparse.Namespace) -> None:
    ndk = find_ndk(args.ndk).resolve()
    toolchain = ndk / "toolchains/llvm/prebuilt/linux-x86_64/bin"
    if not (toolchain / "llvm-ar").is_file():
        raise SystemExit(f"Android NDK toolchain is missing: {toolchain}")
    triple = target_triple(args.abi)
    compiler = toolchain / f"{triple}{args.api}-clang"
    compiler_cpp = toolchain / f"{triple}{args.api}-clang++"
    for program in (compiler, compiler_cpp):
        if not program.is_file():
            raise SystemExit(f"Android compiler is missing: {program}")

    root = Path(__file__).resolve().parents[1]
    cache = root / "scratch/android-deps/source"
    source = source_tree(cache)
    prefix = args.prefix if args.prefix.is_absolute() else root / args.prefix
    prefix = prefix.resolve()
    if not str(prefix).startswith(str((root / "scratch").resolve()) + os.sep):
        raise SystemExit(f"Refusing Android dependency output outside scratch/: {prefix}")

    required = [prefix / "include/libavutil/avutil.h"]
    required.extend(prefix / "lib" / f"lib{name}.a" for name in LIBRARIES)
    if all(path.is_file() for path in required):
        print(f"Android FFmpeg prefix already exists: {prefix}")
        return

    build_dir = root / "scratch/android-deps" / f"build-{args.abi}"
    build_dir.mkdir(parents=True, exist_ok=True)
    configure = [
        str(source / "configure"),
        f"--prefix={prefix}",
        "--target-os=android",
        "--arch=aarch64" if args.abi == "arm64-v8a" else "--arch=x86_64",
        "--cpu=armv8-a" if args.abi == "arm64-v8a" else "--cpu=x86-64",
        "--enable-cross-compile",
        f"--cc={compiler}",
        f"--cxx={compiler_cpp}",
        f"--strip={toolchain / 'llvm-strip'}",
        f"--nm={toolchain / 'llvm-nm'}",
        f"--ar={toolchain / 'llvm-ar'}",
        f"--ranlib={toolchain / 'llvm-ranlib'}",
        f"--sysroot={toolchain / '../sysroot'}",
        "--enable-pic",
        "--enable-static",
        "--disable-shared",
        "--disable-programs",
        "--disable-doc",
        "--disable-debug",
        "--disable-network",
        "--disable-postproc",
        "--disable-avdevice",
        "--disable-avfilter",
        "--disable-everything",
        "--enable-demuxer=mpegps",
        "--enable-decoder=mpeg1video",
        "--enable-decoder=adpcm_adx",
        "--enable-parser=mpegvideo",
        "--enable-protocol=file",
        "--enable-swscale",
        "--enable-swresample",
        "--enable-avformat",
        "--enable-avcodec",
        "--enable-avutil",
        "--disable-autodetect",
        "--disable-iconv",
        "--disable-zlib",
        "--disable-bzlib",
        "--disable-lzma",
        "--disable-sdl2",
        "--disable-vulkan",
        "--disable-vaapi",
        "--disable-vdpau",
        "--disable-videotoolbox",
        "--disable-audiotoolbox",
        "--disable-libdrm",
        "--disable-appkit",
        "--pkg-config=/bin/false",
        "--extra-cflags=-fPIC",
        "--extra-ldflags=-fPIC",
        "--extra-version=x2-android",
    ]
    subprocess.run(configure, cwd=build_dir, check=True)
    subprocess.run(["make", f"-j{args.jobs}"], cwd=build_dir, check=True)
    subprocess.run(["make", "install"], cwd=build_dir, check=True)
    if not all(path.is_file() for path in required):
        raise SystemExit(f"FFmpeg install completed without the required files: {prefix}")
    print(f"Android FFmpeg prefix: {prefix}")


def main() -> int:
    build(parse_args())
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
