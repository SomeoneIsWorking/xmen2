"""Pinned source archives for the native dependency prefixes, and how to fetch them.

Every library a release links comes from one of these archives, checked by
SHA-256 before it is unpacked. `windows_deps.py` and `linux_deps.py` build the
same versions with their own toolchain.
"""

from __future__ import annotations

from dataclasses import dataclass
import hashlib
from pathlib import Path, PurePosixPath
import shutil
import subprocess
import tarfile
import urllib.request
import zipfile


@dataclass(frozen=True)
class Archive:
    name: str
    version: str
    url: str
    sha256: str

    @property
    def filename(self) -> str:
        suffix = next(ending for ending in (".tar.gz", ".tar.xz", ".zip", ".AppImage")
                      if self.url.endswith(ending))
        return f"{self.name}-{self.version}{suffix}"


def github_commit(owner: str, repository: str, commit: str, sha256: str) -> Archive:
    return Archive(repository, commit,
                   f"https://github.com/{owner}/{repository}/archive/{commit}.tar.gz", sha256)


ZLIB = Archive(
    "zlib", "1.3.2",
    "https://github.com/madler/zlib/releases/download/v1.3.2/zlib-1.3.2.tar.gz",
    "bb329a0a2cd0274d05519d61c667c062e06990d72e125ee2dfa8de64f0119d16")
# release-3.4.16 plus an ICCCM WM_NAME, fork branch xmen2/release-3.4.16
SDL3 = github_commit("SomeoneIsWorking", "SDL", "12f35f14892f01b2199c611be23d628e5ff6917e",
                     "8a49f23f43788791b6cdbbbda18cc57688bc9ccb4261411dbdd173a6b8f98cf5")
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

SDL3_OPTIONS = (
    "-DSDL_SHARED=OFF", "-DSDL_STATIC=ON", "-DSDL_TESTS=OFF",
    "-DSDL_TEST_LIBRARY=OFF", "-DSDL_EXAMPLES=OFF", "-DSDL_INSTALL=ON")
SDL3_IMAGE_OPTIONS = (
    "-DBUILD_SHARED_LIBS=OFF", "-DSDLIMAGE_VENDORED=OFF", "-DSDLIMAGE_DEPS_SHARED=OFF",
    "-DSDLIMAGE_BACKEND_STB=ON", "-DSDLIMAGE_BACKEND_WIC=OFF", "-DSDLIMAGE_SAMPLES=OFF",
    "-DSDLIMAGE_TESTS=OFF", "-DSDLIMAGE_PNG_LIBPNG=OFF", "-DSDLIMAGE_AVIF=OFF", "-DSDLIMAGE_JXL=OFF",
    "-DSDLIMAGE_TIF=OFF", "-DSDLIMAGE_WEBP=OFF", "-DSDLIMAGE_INSTALL=ON")
FREETYPE_OPTIONS = (
    "-DBUILD_SHARED_LIBS=OFF", "-DFT_DISABLE_BZIP2=ON", "-DFT_DISABLE_BROTLI=ON",
    "-DFT_DISABLE_HARFBUZZ=ON", "-DFT_DISABLE_PNG=ON", "-DFT_DISABLE_ZLIB=ON")

# The container, codecs and parser the title's SFD movies and WMA streams use.
FFMPEG_FEATURES = (
    "--enable-demuxer=mpegps,asf",
    "--enable-decoder=mpeg1video,adpcm_adx,wmav1,wmav2,wmapro,wmavoice",
    "--enable-parser=mpegvideo",
    "--enable-protocol=file",
)
FFMPEG_COMMON_OPTIONS = (
    "--enable-static", "--disable-shared", "--disable-programs", "--disable-doc",
    "--disable-debug", "--disable-network", "--disable-avdevice",
    "--disable-avfilter", "--disable-everything", "--disable-autodetect",
    "--disable-iconv", "--disable-zlib", "--disable-x86asm",
    *FFMPEG_FEATURES, "--enable-swscale", "--enable-swresample")
FFMPEG_LIBRARIES = ("avformat", "avcodec", "swscale", "swresample", "avutil")


def refuse(message: str) -> None:
    raise SystemExit(f"deps: {message}")


def run(command: list[str], cwd: Path | None = None, env: dict[str, str] | None = None) -> None:
    print("+", " ".join(command), flush=True)
    result = subprocess.run(command, cwd=cwd, env=env)
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
        print(f"deps: downloading {archive.url}", flush=True)
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
