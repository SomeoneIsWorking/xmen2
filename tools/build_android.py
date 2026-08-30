#!/usr/bin/env python3
"""Configure, build, and assemble the Android APK from a cold native tree."""

from __future__ import annotations

import argparse
import os
from pathlib import Path
import shutil
import subprocess
import sys


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--abi", default="arm64-v8a", choices=("arm64-v8a", "x86_64"))
    parser.add_argument("--api", type=int, default=34)
    parser.add_argument("--build-dir", type=Path)
    parser.add_argument("--skip-deps", action="store_true")
    parser.add_argument("--no-assemble", action="store_true")
    return parser.parse_args()


def ndk_path() -> Path:
    sdk = os.environ.get("ANDROID_HOME") or os.environ.get("ANDROID_SDK_ROOT")
    version = os.environ.get("ANDROID_NDK_VERSION")
    if not sdk or not version:
        raise SystemExit(
            "Android requires ANDROID_HOME and ANDROID_NDK_VERSION. "
            "Install/select the NDK before building the APK."
        )
    path = Path(sdk) / "ndk" / version
    if not (path / "build/cmake/android.toolchain.cmake").is_file():
        raise SystemExit(f"Android NDK toolchain is missing: {path}")
    return path


def run(command: list[str], *, cwd: Path) -> None:
    print("+", " ".join(command))
    subprocess.run(command, cwd=cwd, check=True)


def java_home() -> Path:
    candidates: list[Path] = []
    configured = os.environ.get("JAVA_HOME")
    if configured:
        candidates.append(Path(configured))
    java = shutil.which("java")
    if java:
        candidates.append(Path(java).resolve().parent.parent)
    candidates.extend(Path("/usr/lib/jvm").glob("*/bin/java"))
    seen: set[Path] = set()
    for candidate in candidates:
        home = candidate if candidate.name != "java" else candidate.parent.parent
        home = home.resolve()
        if (home in seen or not (home / "bin/java").is_file()
                or not (home / "bin/javac").is_file()):
            continue
        seen.add(home)
        result = subprocess.run([str(home / "bin/java"), "-version"],
                                capture_output=True, text=True)
        version = result.stderr + result.stdout
        first = version.splitlines()[0] if version.splitlines() else ""
        try:
            major_text = first.split('version "', 1)[1].split(".", 1)[0]
            major = int(major_text)
        except (IndexError, ValueError):
            continue
        if 17 <= major <= 26:
            return home
    raise SystemExit(
        "Android Gradle build needs a JDK from 17 through 26, but no supported "
        "JDK was found. On this DNF system install it with: "
        "sudo dnf install java-17-openjdk-devel; then set JAVA_HOME to that JDK."
    )


def main() -> int:
    args = parse_args()
    root = Path(__file__).resolve().parents[1]
    build = args.build_dir or root / "scratch" / f"build-android-{args.abi}"
    build = build if build.is_absolute() else root / build
    if not str(build.resolve()).startswith(str((root / "scratch").resolve()) + os.sep):
        raise SystemExit(f"Refusing Android build output outside scratch/: {build}")
    build.mkdir(parents=True, exist_ok=True)
    ndk = ndk_path()
    gradle_java = java_home()
    prefix = root / "scratch" / "android-deps" / args.abi
    if not args.skip_deps:
        run(
            [
                sys.executable,
                str(root / "tools/build_android_deps.py"),
                "--abi",
                args.abi,
                "--api",
                str(args.api),
                "--ndk",
                str(ndk),
                "--prefix",
                str(prefix),
            ],
            cwd=root,
        )

    run(
        [
            "cmake",
            "-S",
            str(root),
            "-B",
            str(build),
            f"-DCMAKE_TOOLCHAIN_FILE={ndk / 'build/cmake/android.toolchain.cmake'}",
            f"-DANDROID_ABI={args.abi}",
            f"-DANDROID_PLATFORM=android-{args.api}",
            "-DANDROID_STL=c++_shared",
            "-DCMAKE_BUILD_TYPE=Release",
            f"-DPython3_EXECUTABLE={sys.executable}",
            f"-DX2_ANDROID_FFMPEG_ROOT={prefix}",
        ],
        cwd=root,
    )
    run(["cmake", "--build", str(build), "--target", "x2native", "-j2"], cwd=root)
    if not args.no_assemble:
        run(
            [
                "./gradlew",
                "--no-daemon",
                "-Dorg.gradle.java.home=" + str(gradle_java),
                "-Px2NativeProperties=" + str(build / "x2-android.properties"),
                ":app:assembleRelease",
            ],
            cwd=root / "android",
        )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
