#!/usr/bin/env python3
"""Build the release AppImage inside the Ubuntu 22.04 build container.

The image is `packaging/appimage-build.Containerfile`, tagged by its own hash.
The checkout is mounted at its host path; the build, its uv environment and
the dependency prefix stay under `build/`, apart from the host's own build
tree and `.venv`. Locally this runs under podman; CI passes `--engine docker`.
"""

from __future__ import annotations

import argparse
import hashlib
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tarfile

from dependency_archives import Archive, fetch, refuse, run


ROOT = Path(__file__).resolve().parents[1]
CONTAINERFILE = ROOT / "packaging" / "appimage-build.Containerfile"
IMAGE = "localhost/xmen2-appimage-build"
TOOLS = ROOT / "build" / "deps" / "appimage-tools"
BUILD = ROOT / "build" / "appimage"
OUTPUT = ROOT / "build" / "release" / "X-Men-Legends-II-x86_64.AppImage"

LINUXDEPLOY = Archive(
    "linuxdeploy", "1-alpha-20251107-1",
    "https://github.com/linuxdeploy/linuxdeploy/releases/download/1-alpha-20251107-1/"
    "linuxdeploy-x86_64.AppImage",
    "c20cd71e3a4e3b80c3483cef793cda3f4e990aca14014d23c544ca3ce1270b4d")
APPIMAGETOOL = Archive(
    "appimagetool", "1.9.1",
    "https://github.com/AppImage/appimagetool/releases/download/1.9.1/appimagetool-x86_64.AppImage",
    "ed4ce84f0d9caff66f50bcca6ff6f35aae54ce8135408b3fa33abfc3cb384eb0")
# Ubuntu's patchelf predates 0.19 and corrupts DT_INIT; package_appimage.py refuses it.
PATCHELF = Archive(
    "patchelf", "0.19.1",
    "https://github.com/NixOS/patchelf/releases/download/0.19.1/patchelf-0.19.1-x86_64.tar.gz",
    "a6818fef80128fb354423234ecacdcca3e993913d774e5d8346bc63f70fed4cf")


def patchelf_binary() -> Path:
    destination = TOOLS / f"patchelf-{PATCHELF.version}"
    with tarfile.open(fetch(PATCHELF, TOOLS)) as bundle:
        member = bundle.getmember("./bin/patchelf")
        bundle.extract(member, destination, filter="data")
    return destination / "bin" / "patchelf"


def packaging_tools() -> dict[str, Path]:
    linuxdeploy = fetch(LINUXDEPLOY, TOOLS)
    appimagetool = fetch(APPIMAGETOOL, TOOLS)
    patchelf = patchelf_binary()
    for path in (linuxdeploy, appimagetool, patchelf):
        path.chmod(0o755)
    return {"linuxdeploy": linuxdeploy, "appimagetool": appimagetool, "patchelf": patchelf}


def image_tag() -> str:
    digest = hashlib.sha256(CONTAINERFILE.read_bytes()).hexdigest()[:12]
    return f"{IMAGE}:{digest}"


def engine_options(engine: str) -> list[str]:
    if engine == "podman":
        # Keep host ownership; SELinux labels are left alone instead of relabelling the checkout.
        return ["--userns=keep-id", "--security-opt", "label=disable"]
    return ["--user", f"{os.getuid()}:{os.getgid()}"]


def build_image(engine: str, tag: str) -> None:
    present = subprocess.run([engine, "image", "inspect", tag], capture_output=True, check=False)
    if present.returncode:
        run([engine, "build", "-t", tag, "-f", str(CONTAINERFILE), str(CONTAINERFILE.parent)])


def in_container(engine: str, tag: str, command: list[str]) -> None:
    home = ROOT / "build" / "appimage-home"
    home.mkdir(parents=True, exist_ok=True)
    run([engine, "run", "--rm", *engine_options(engine),
         "-v", f"{ROOT}:{ROOT}", "-w", str(ROOT),
         "-e", f"HOME={home}",
         "-e", f"UV_PROJECT_ENVIRONMENT={ROOT / 'build' / 'appimage-venv'}",
         tag, *command])


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--engine", choices=("podman", "docker"), default="podman")
    parser.add_argument("--output", type=Path, default=OUTPUT)
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    if not shutil.which(args.engine):
        refuse(f"{args.engine} is required to build the AppImage")
    output = args.output.resolve()
    if ROOT not in output.parents:
        refuse(f"--output must be inside the checkout the container mounts: {output}")
    tools = packaging_tools()
    tag = image_tag()
    build_image(args.engine, tag)
    in_container(args.engine, tag, [
        "uv", "run", "--frozen", "python", "tools/ci.py", "release-binary",
        "--target", "linux-x86_64", "--build-dir", str(BUILD)])
    in_container(args.engine, tag, [
        "uv", "run", "--frozen", "python", "tools/package_appimage.py",
        "--build-dir", str(BUILD), "--output", str(output),
        *(f"--{name}={path}" for name, path in tools.items())])
    print(f"appimage_container: {output}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
