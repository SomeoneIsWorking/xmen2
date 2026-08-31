#!/usr/bin/env python3
"""Configure, build, and assemble the Android APK from a cold native tree."""

from __future__ import annotations

import argparse
from collections.abc import Mapping
import os
from pathlib import Path
import shutil
import subprocess
import sys

try:
    from .shared_dir import shared_dir
except ImportError:
    from shared_dir import shared_dir


GRADLE_VERSION = "9.4.1"
GRADLE_JAVA_MIN = 17
GRADLE_JAVA_MAX = 26
DEFAULT_NATIVE_JOBS = 2
NATIVE_GENERATOR = "Ninja"
DEFAULT_ANDROID_API = 21


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--abi", default="arm64-v8a", choices=("arm64-v8a", "x86_64"))
    parser.add_argument("--api", type=int, default=DEFAULT_ANDROID_API)
    parser.add_argument("--build-dir", type=Path)
    parser.add_argument("--skip-deps", action="store_true")
    parser.add_argument("--no-assemble", action="store_true")
    parser.add_argument(
        "--debug",
        action="store_true",
        help="Assemble a debug-signed APK for local device testing. The "
             "artifact stays in Gradle's build output and is never published "
             "to build/release; it is not a release candidate.",
    )
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


def android_port_tool() -> Path:
    root = Path(shared_dir("android-port", "tools/android_port.py"))
    return root / "tools" / "android_port.py"


def native_prefix(build_root: Path, api: int, abi: str) -> Path:
    """Return the ABI and Android-API-specific shared native dependency prefix."""
    return build_root / "deps/android" / f"android-{api}" / abi


def parse_java_major(version_output: str) -> int | None:
    first = version_output.splitlines()[0] if version_output.splitlines() else ""
    try:
        version = first.split('version "', 1)[1].split('"', 1)[0]
        components = version.split(".")
        return int(components[1] if components[0] == "1" else components[0])
    except (IndexError, ValueError):
        return None


def parse_javac_major(version_output: str) -> int | None:
    first = version_output.splitlines()[0] if version_output.splitlines() else ""
    try:
        return int(first.split("javac ", 1)[1].split(".", 1)[0])
    except (IndexError, ValueError):
        return None


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
        java = subprocess.run([str(home / "bin/java"), "-version"],
                              capture_output=True, text=True)
        javac = subprocess.run([str(home / "bin/javac"), "-version"],
                               capture_output=True, text=True)
        java_major = parse_java_major(java.stderr + java.stdout)
        javac_major = parse_javac_major(javac.stderr + javac.stdout)
        if (java_major == javac_major and java_major is not None
                and GRADLE_JAVA_MIN <= java_major <= GRADLE_JAVA_MAX):
            return home
    raise SystemExit(
        f"Gradle {GRADLE_VERSION} needs a coherent JDK from {GRADLE_JAVA_MIN} "
        f"through {GRADLE_JAVA_MAX}, but no home with matching java and javac "
        "versions was found. Select one with JAVA_HOME."
    )


def release_signing(environment: Mapping[str, str] = os.environ) -> dict[str, str]:
    names = (
        "X2_ANDROID_KEYSTORE",
        "X2_ANDROID_KEY_ALIAS",
        "X2_ANDROID_STORE_PASSWORD",
        "X2_ANDROID_KEY_PASSWORD",
    )
    values = {name: environment.get(name, "") for name in names}
    missing = [name for name, value in values.items() if not value]
    if missing:
        raise SystemExit(
            "Android release signing is incomplete; set " + ", ".join(missing) +
            ". Refusing to produce an unsigned release APK."
        )
    keystore = Path(values["X2_ANDROID_KEYSTORE"]).expanduser()
    if not keystore.is_file():
        raise SystemExit(f"Android release keystore is missing: {keystore}")
    values["X2_ANDROID_KEYSTORE"] = str(keystore.resolve())
    return values


def native_jobs(environment: Mapping[str, str] = os.environ) -> int:
    """Return a memory-safe parallelism level for translated Android code.

    A translated X-Men module can consume more than a gigabyte while Clang
    optimizes it.  Building one job per logical CPU turns a 16 GB workstation
    into a swap storm, so Android builds default to two jobs.  Builders with
    measured headroom can deliberately raise the cap.
    """
    configured = environment.get("X2_ANDROID_NATIVE_JOBS")
    if configured is None:
        return DEFAULT_NATIVE_JOBS
    try:
        jobs = int(configured)
    except ValueError as error:
        raise SystemExit("X2_ANDROID_NATIVE_JOBS must be a positive integer") from error
    if jobs <= 0:
        raise SystemExit("X2_ANDROID_NATIVE_JOBS must be a positive integer")
    return jobs


def cached_generator(build: Path) -> str | None:
    """Return CMake's recorded generator, refusing no state as no generator."""
    cache = build / "CMakeCache.txt"
    if not cache.is_file():
        return None
    for line in cache.read_text(encoding="utf-8").splitlines():
        if line.startswith("CMAKE_GENERATOR:INTERNAL="):
            return line.partition("=")[2]
    raise SystemExit(f"Android CMake cache has no generator declaration: {cache}")


def prepare_native_build_directory(build: Path, build_root: Path) -> None:
    """Migrate the generated Android tree to Ninja when CMake recorded another generator.

    Makefiles conservatively make every object depend on their regenerated
    flags.make. Consequently an otherwise harmless CMake reconfigure rebuilds
    the whole translated game. Ninja compares the real compiler command instead.
    A generator cannot be changed in place, so this removes only the resolved,
    generated Android tree beneath the project's build root.
    """
    resolved_build = build.resolve()
    resolved_root = build_root.resolve()
    if resolved_build.parent != resolved_root:
        raise SystemExit(
            f"Refusing Android build migration outside direct build/ child: {build}")
    generator = cached_generator(resolved_build)
    if generator is None or generator == NATIVE_GENERATOR:
        resolved_build.mkdir(parents=True, exist_ok=True)
        return
    print(f"android: replacing {generator} build tree with {NATIVE_GENERATOR}: "
          f"{resolved_build}")
    shutil.rmtree(resolved_build)
    resolved_build.mkdir(parents=True, exist_ok=True)


def apksigner_path() -> Path:
    sdk_value = os.environ.get("ANDROID_HOME") or os.environ.get("ANDROID_SDK_ROOT")
    if not sdk_value:
        raise SystemExit("ANDROID_HOME or ANDROID_SDK_ROOT is required")
    tools = Path(sdk_value) / "build-tools"
    candidates = sorted(tools.glob("*/apksigner"), reverse=True)
    if not candidates:
        raise SystemExit(f"Android apksigner is missing under {tools}")
    return candidates[0]


def debug_apk(root: Path, abi: str) -> Path:
    """The debug artifact, which stays in Gradle's output and is never published."""
    outputs = root / "android/app/build/outputs/apk/debug"
    candidates = sorted(outputs.glob(f"*{abi}-debug.apk"))
    if len(candidates) != 1:
        found = ", ".join(path.name for path in sorted(outputs.glob(f"*{abi}*.apk")))
        raise SystemExit(
            f"Expected exactly one {abi} debug APK in {outputs}; found: {found or 'none'}"
        )
    return candidates[0]


def publish_apk(root: Path, abi: str) -> Path:
    outputs = root / "android/app/build/outputs/apk/release"
    candidates = [path for path in outputs.glob("*-release.apk")
                  if "unsigned" not in path.name]
    if len(candidates) != 1:
        found = ", ".join(path.name for path in sorted(outputs.glob("*.apk")))
        raise SystemExit(
            f"Expected exactly one signed release APK in {outputs}; found: {found or 'none'}"
        )
    signer = apksigner_path()
    run([str(signer), "verify", "--verbose", "--print-certs",
         str(candidates[0])], cwd=root)
    release = root / "build/release"
    release.mkdir(parents=True, exist_ok=True)
    destination = release / f"X-Men-Legends-II-{abi}.apk"
    shutil.copy2(candidates[0], destination)
    print(f"android: created signed release {destination} ({destination.stat().st_size} bytes)")
    return destination


def main() -> int:
    args = parse_args()
    root = Path(__file__).resolve().parents[1]
    build_root = root / "build"
    build = args.build_dir or build_root / f"android-{args.abi}"
    build = build if build.is_absolute() else root / build
    if not str(build.resolve()).startswith(str(build_root.resolve()) + os.sep):
        raise SystemExit(f"Refusing Android build output outside build/: {build}")
    prepare_native_build_directory(build, build_root)
    ndk = ndk_path()
    gradle_java = java_home() if not args.no_assemble else None
    signing = release_signing() if not args.no_assemble and not args.debug else None
    prefix = native_prefix(build_root, args.api, args.abi)
    if not args.skip_deps:
        run(
            [
                sys.executable,
                str(android_port_tool()),
                "build-native-deps",
                "--ndk",
                str(ndk),
                "--prefix",
                str(prefix),
                "--abi",
                args.abi,
                "--api",
                str(args.api),
                "--jobs",
                str(native_jobs()),
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
            "-G",
            NATIVE_GENERATOR,
            f"-DCMAKE_TOOLCHAIN_FILE={ndk / 'build/cmake/android.toolchain.cmake'}",
            f"-DANDROID_ABI={args.abi}",
            f"-DANDROID_PLATFORM=android-{args.api}",
            "-DANDROID_STL=c++_shared",
            "-DCMAKE_BUILD_TYPE=Release",
            # A diagnostic cache entry must never leak from a local Android
            # investigation into the packaged product tree.
            "-DX2_NATIVE_TRACE=OFF",
            f"-DPython3_EXECUTABLE={sys.executable}",
            f"-DX2_ANDROID_PORT_PREFIX={prefix}",
        ],
        cwd=root,
    )
    run(["cmake", "--build", str(build), "--target", "x2native", f"-j{native_jobs()}"], cwd=root)
    if not args.no_assemble:
        assert gradle_java is not None
        gradle_environment = os.environ.copy()
        task = ":app:assembleDebug" if args.debug else ":app:assembleRelease"
        if args.debug:
            print(f"+ ./gradlew --no-daemon {task}")
        else:
            assert signing is not None
            gradle_environment.update(signing)
            print(f"+ ./gradlew --no-daemon <release signing hidden> {task}")
        subprocess.run(
            [
                "./gradlew",
                "--no-daemon",
                "-Dorg.gradle.java.home=" + str(gradle_java),
                "-Px2NativeProperties=" + str(build / "x2-android.properties"),
                task,
            ],
            cwd=root / "android",
            env=gradle_environment,
            check=True,
        )
        if args.debug:
            print(f"android: debug APK left in {debug_apk(root, args.abi)} (not a release)")
        else:
            publish_apk(root, args.abi)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
