#!/usr/bin/env python3
"""Configure, build, and assemble the Android APK from a cold native tree."""

from __future__ import annotations

import argparse
import importlib.util
from functools import cache
from collections.abc import Mapping
import os
import re
from pathlib import Path
import shutil
import json
import subprocess
import sys
from types import ModuleType

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import bootstrap

try:
    from .shared_dir import shared_dir
except ImportError:
    from shared_dir import shared_dir


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
    # The Android build may be run without the launcher; still enforce its pin.
    required = next(repo for repo in bootstrap.SHARED_REPOS if repo.name == "android-port")
    bootstrap.validate_checkout(required, root)
    return root / "tools" / "android_port.py"


def native_prefix(build_root: Path, api: int, abi: str) -> Path:
    """Return the ABI and Android-API-specific shared native dependency prefix."""
    return build_root / "deps/android" / f"android-{api}" / abi


@cache
def shared_android() -> ModuleType:
    tool = android_port_tool()
    spec = importlib.util.spec_from_file_location("x2_shared_android", tool)
    if spec is None or spec.loader is None:
        raise SystemExit(f"Cannot load the shared Android build owner: {tool}")
    module = importlib.util.module_from_spec(spec)
    sys.modules[spec.name] = module
    spec.loader.exec_module(module)
    return module


def java_home() -> Path:
    return shared_android().select_java_home(minimum=GRADLE_JAVA_MIN, maximum=GRADLE_JAVA_MAX)


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
            "Android release signing is incomplete; set "
            + ", ".join(missing)
            + ". Refusing to produce an unsigned release APK."
        )
    keystore = Path(values["X2_ANDROID_KEYSTORE"]).expanduser()
    if not keystore.is_file():
        raise SystemExit(f"Android release keystore is missing: {keystore}")
    values["X2_ANDROID_KEYSTORE"] = str(keystore.resolve())
    return values


def native_jobs(environment: Mapping[str, str] = os.environ) -> int:
    """Bound parallel compiler memory use; measured hosts may opt into more jobs."""
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
    every native object. Ninja compares the real compiler command instead.
    A generator cannot be changed in place, so this removes only the resolved,
    generated Android tree beneath the project's build root.
    """
    resolved_build = build.resolve()
    resolved_root = build_root.resolve()
    if resolved_build.parent != resolved_root:
        raise SystemExit(f"Refusing Android build migration outside direct build/ child: {build}")
    generator = cached_generator(resolved_build)
    if generator is None or generator == NATIVE_GENERATOR:
        resolved_build.mkdir(parents=True, exist_ok=True)
        return
    print(f"android: replacing {generator} build tree with {NATIVE_GENERATOR}: {resolved_build}")
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


def apk_version_code(apk: Path) -> int:
    """The versionCode the built APK actually declares."""
    aapt2 = apksigner_path().parent / "aapt2"
    if not aapt2.is_file():
        raise SystemExit(f"Android aapt2 is missing beside apksigner: {aapt2}")
    badging = subprocess.run(
        [str(aapt2), "dump", "badging", str(apk)],
        check=True,
        capture_output=True,
        text=True,
    ).stdout
    match = re.search(r"versionCode='(\d+)'", badging)
    if not match:
        raise SystemExit(f"aapt2 reported no versionCode for {apk.name}")
    return int(match.group(1))


def signer_digest(apk: Path) -> str:
    """The SHA-256 of the certificate an APK is signed with."""
    signer = apksigner_path()
    output = subprocess.run(
        [str(signer), "verify", "--print-certs", str(apk)],
        check=True,
        capture_output=True,
        text=True,
    ).stdout
    digests = [
        line.rsplit(":", 1)[1].strip().lower()
        for line in output.splitlines()
        if "certificate SHA-256 digest" in line
    ]
    if len(digests) != 1:
        raise SystemExit(
            f"Expected exactly one signer certificate in {apk.name}; "
            f"apksigner reported {len(digests)}. An APK with no stable single "
            "signer cannot update an installed copy."
        )
    return digests[0]


def published_record(root: Path) -> Path:
    return root / "android/published-release.json"


def require_publishable(root: Path, apk: Path, version_code: int) -> str:
    """
    Refuse an APK that an already-installed copy cannot accept as an update.

    Android identifies an application by package name AND signing certificate.
    A package signed with a different key is a DIFFERENT application that
    happens to share a name, so the installer refuses it -- "App not installed
    as package conflicts with an existing package" -- and the only route
    forward it offers is uninstalling, which deletes app-private storage. For
    this port that is the player's entire imported game installation, gigabytes
    of it, and they have to import it again.

    That is exactly what shipping v0.1.7 and v0.2.0 under two different
    machine-local debug keys did. The signing identity of a published APK is
    therefore a promise to everyone who installed the last one, and this is the
    gate that keeps it. The recorded digest is a public certificate
    fingerprint, not a secret; the private key never appears here.
    """
    digest = signer_digest(apk)
    record = published_record(root)
    if not record.is_file():
        raise SystemExit(
            f"No published signing identity is recorded in {record}.\n"
            f"This APK is signed with SHA-256 {digest}.\n"
            "Record it deliberately, with the long-lived maintainer key, before "
            "the first publication -- every later release must match it, and a "
            "key that is lost or regenerated can never update an installed copy "
            "again."
        )
    expected = json.loads(record.read_text())["signer_sha256"].lower()
    if expected != digest:
        raise SystemExit(
            "Refusing to publish: this APK's signing certificate does not match "
            "the published one.\n"
            f"  published: {expected}\n"
            f"  this APK:  {digest}\n"
            "Everyone who installed the last release would have to uninstall -- "
            "losing their imported game installation -- to accept this one. Sign "
            "with the long-lived maintainer key."
        )
    if version_code < 1:
        raise SystemExit(
            f"Refusing to publish: versionCode {version_code} is not a real "
            "version. Two releases that claim one version cannot be told apart "
            "on a device. Set X2_ANDROID_VERSION_CODE."
        )
    return digest


def publish_apk(root: Path, abi: str) -> Path:
    outputs = root / "android/app/build/outputs/apk/release"
    candidates = [path for path in outputs.glob("*-release.apk") if "unsigned" not in path.name]
    if len(candidates) != 1:
        found = ", ".join(path.name for path in sorted(outputs.glob("*.apk")))
        raise SystemExit(
            f"Expected exactly one signed release APK in {outputs}; found: {found or 'none'}"
        )
    shared_android().inspect_apk_runtime(candidates[0], abi)
    signer = apksigner_path()
    run([str(signer), "verify", "--verbose", "--print-certs", str(candidates[0])], cwd=root)
    version_code = apk_version_code(candidates[0])
    digest = require_publishable(root, candidates[0], version_code)
    release = root / "build/release"
    release.mkdir(parents=True, exist_ok=True)
    destination = release / f"X-Men-Legends-II-{abi}.apk"
    shutil.copy2(candidates[0], destination)
    print(f"android: created signed release {destination} ({destination.stat().st_size} bytes)")
    print(f"android: signer {digest}, versionCode {version_code}")
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
            f"-DPython3_EXECUTABLE={sys.executable}",
            f"-DX2_ANDROID_PORT_PREFIX={prefix}",
        ],
        cwd=root,
    )
    run(["cmake", "--build", str(build), "--target", "x2native", f"-j{native_jobs()}"], cwd=root)
    shared_android().verify_native_entry(build / "libmain.so", ndk)
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
            apk = debug_apk(root, args.abi)
            shared_android().inspect_apk_runtime(apk, args.abi)
            print(f"android: debug APK left in {apk} (not a release)")
        else:
            publish_apk(root, args.abi)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
