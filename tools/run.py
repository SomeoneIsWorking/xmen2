#!/usr/bin/env python3
"""Build and launch the one live native target selected by ``run.sh``."""

from __future__ import annotations

import ctypes.util
from dataclasses import dataclass
import os
from pathlib import Path
import platform
import re
import shutil
import subprocess
import sys
from typing import Callable


ROOT = Path(__file__).resolve().parent.parent


def refuse(message: str) -> None:
    raise SystemExit(f"run: {message}")


def load_dotenv() -> None:
    path = ROOT / ".env"
    if not path.is_file():
        return
    for line in path.read_text().splitlines():
        match = re.fullmatch(r"\s*(?:export\s+)?([A-Za-z_][A-Za-z0-9_]*)=(.*)\s*", line)
        if not match:
            continue
        key, raw = match.groups()
        value = raw.strip().strip('"').strip("'")
        if key not in os.environ and value and not value.startswith("/path/to"):
            os.environ[key] = value


@dataclass(frozen=True)
class Toolchain:
    cmake: str
    ninja: str
    cc: str
    cxx: str
    cmake_compiler_id: str


def resolve_toolchain(which: Callable[[str], str | None] = shutil.which,
                      environment: dict[str, str] | None = None) -> Toolchain:
    env = os.environ if environment is None else environment
    cmake, ninja = which("cmake"), which("ninja")
    missing = [name for name, value in (("cmake", cmake), ("ninja", ninja)) if not value]
    if missing:
        refuse("the locked uv environment is missing " + ", ".join(missing)
               + "; run `uv sync --frozen` and retry")
    configured = env.get("CC"), env.get("CXX")
    if bool(configured[0]) != bool(configured[1]):
        refuse(f"CC and CXX must be set together (CC={configured[0]!r}, CXX={configured[1]!r})")
    if configured[0] and configured[1]:
        cc, cxx = which(configured[0]), which(configured[1])
        if not cc or not cxx:
            refuse(f"configured compiler pair was not found: CC={configured[0]}, CXX={configured[1]}")
    else:
        cc = cxx = None
        for cc_name, cxx_name in (("clang", "clang++"), ("gcc", "g++"), ("cc", "c++")):
            candidate = which(cc_name), which(cxx_name)
            if all(candidate):
                cc, cxx = candidate
                break
    if not cc or not cxx:
        refuse("no complete C/C++ compiler pair found. Install one and re-run:\n\n       "
               + compiler_package_command())
    name = Path(cxx).name.casefold()
    compiler_id = "Clang" if "clang" in name else "GNU"
    return Toolchain(cmake or "", ninja or "", cc, cxx, compiler_id)


def os_release_identities() -> set[str]:
    values: dict[str, str] = {}
    path = Path("/etc/os-release")
    if path.is_file():
        for line in path.read_text().splitlines():
            if "=" in line:
                key, value = line.split("=", 1)
                values[key] = value.strip().strip('"')
    return {values.get("ID", ""), *values.get("ID_LIKE", "").split()}


def compiler_package_command() -> str:
    identities = os_release_identities()
    if identities & {"fedora", "rhel"}:
        return "sudo dnf install clang"
    if identities & {"debian", "ubuntu"}:
        return "sudo apt install clang"
    return "install Clang or GCC, or set CC and CXX to a compatible compiler pair"


def platform_package_command() -> str:
    if platform.system() == "Darwin":
        return ("brew install sdl3 sdl3_image ffmpeg freetype shaderc "
                "pkg-config molten-vk vulkan-loader")
    identities = os_release_identities()
    if identities & {"fedora", "rhel"}:
        return ("sudo dnf install SDL3-devel SDL3_image-devel ffmpeg-free-devel "
                "freetype-devel glslc pkgconf-pkg-config vulkan-loader "
                "mesa-vulkan-drivers")
    if identities & {"debian", "ubuntu"}:
        return ("sudo apt install libsdl3-dev libsdl3-image-dev libavformat-dev "
                "libavcodec-dev libavutil-dev libswscale-dev libswresample-dev "
                "libfreetype-dev glslc pkg-config libvulkan1 mesa-vulkan-drivers")
    return "install SDL3, SDL3_image, FFmpeg development libraries, FreeType, glslc, pkg-config and Vulkan"


def require_supported_host() -> None:
    system = platform.system()
    machine = platform.machine().lower()
    if system == "Darwin" and machine in {"arm64", "aarch64"}:
        return
    if system == "Windows":
        refuse("native Windows is not supported: the host currently requires POSIX memory, "
               "process, signal and pthread APIs. No winget/vcpkg command can fix that")
    if system != "Linux" or machine not in {"x86_64", "amd64"}:
        refuse(f"the native host currently supports Linux x86-64 or Apple Silicon macOS, "
               f"not {system} {machine}")


def command_ok(arguments: list[str], cwd: Path | None = None) -> bool:
    try:
        return subprocess.run(arguments, cwd=cwd, stdout=subprocess.DEVNULL,
                              stderr=subprocess.DEVNULL).returncode == 0
    except OSError:
        return False


def check_native_dependencies(toolchain: Toolchain) -> None:
    missing: list[str] = []
    probe_directory = ROOT / "scratch/dependency-probe"
    probe_directory.mkdir(parents=True, exist_ok=True)
    if not shutil.which("pkg-config"):
        missing.append("pkg-config")
    else:
        packages = ("sdl3", "libavformat", "libavcodec", "libavutil",
                    "libswscale", "libswresample", "freetype2")
        missing.extend(package for package in packages
                       if not command_ok(["pkg-config", "--exists", package]))
    for package in ("SDL3", "SDL3_image", "Freetype"):
        if not command_ok([toolchain.cmake, "--find-package", f"-DNAME={package}",
                           f"-DCOMPILER_ID={toolchain.cmake_compiler_id}",
                           "-DLANGUAGE=CXX", "-DMODE=EXIST"],
                          cwd=probe_directory):
            missing.append(package)
    if not shutil.which("glslc"):
        missing.append("glslc")
    # The locked uv Python does not consult Homebrew's library directory in
    # ctypes.util.find_library(), even though CMake and the linker can consume
    # vulkan-loader through its installed pkg-config metadata.  Accept either
    # discovery route; requiring ctypes alone made a valid Apple Silicon setup
    # print the same brew command the user had already completed.
    vulkan_pkg = (shutil.which("pkg-config") is not None
                  and command_ok(["pkg-config", "--exists", "vulkan"]))
    if not vulkan_pkg and not ctypes.util.find_library("vulkan"):
        missing.append("Vulkan loader")
    if missing:
        refuse("missing native dependency/dependencies: " + ", ".join(sorted(set(missing)))
               + "\n     Install them and re-run:\n\n       " + platform_package_command())


def build_directory() -> Path:
    configured = os.environ.get("BUILD")
    return Path(configured).expanduser() if configured else ROOT / "build/native"


def prepare_build_directory(path: Path, toolchain: Toolchain) -> None:
    cache = path / "CMakeCache.txt"
    if not cache.is_file():
        return
    values: dict[str, str] = {}
    for line in cache.read_text(errors="replace").splitlines():
        if "=" not in line or line.startswith(("#", "//")):
            continue
        field, value = line.split("=", 1)
        values[field.split(":", 1)[0]] = value
    expected = {"CMAKE_GENERATOR": "Ninja", "CMAKE_C_COMPILER": toolchain.cc,
                "CMAKE_CXX_COMPILER": toolchain.cxx,
                "Python3_EXECUTABLE": sys.executable}
    incompatible = []
    for key, wanted in expected.items():
        found = values.get(key)
        if found and (found if key == "CMAKE_GENERATOR" else os.path.abspath(found)) != (
                wanted if key == "CMAKE_GENERATOR" else os.path.abspath(wanted)):
            incompatible.append(f"{key}={found} (need {wanted})")
    if not incompatible:
        return
    build_root = (ROOT / "build").resolve()
    target = path.resolve()
    if target == build_root or build_root not in target.parents:
        refuse(f"incompatible build cache is outside project build root: {target}; "
               + "; ".join(incompatible))
    print("run: replacing incompatible project-local build cache: " + "; ".join(incompatible))
    shutil.rmtree(target)


def tee_process(arguments: list[str], log: Path, cwd: Path = ROOT,
                environment: dict[str, str] | None = None) -> int:
    log.parent.mkdir(parents=True, exist_ok=True)
    process = subprocess.Popen(arguments, cwd=cwd, env=environment or os.environ,
                               stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    if process.stdout is None:
        refuse("could not capture child output")
    with log.open("wb") as destination:
        for line in iter(process.stdout.readline, b""):
            destination.write(line)
            destination.flush()
            sys.stdout.buffer.write(line)
            sys.stdout.buffer.flush()
    return process.wait()


def run_native() -> int:
    require_supported_host()
    game_value = os.environ.get("GAME_PC_DIR")
    if not game_value or not Path(game_value).is_dir():
        refuse("GAME_PC_DIR is not a directory; set it in .env or drop the install at ./game")
    toolchain = resolve_toolchain()
    check_native_dependencies(toolchain)
    assets = os.environ.get("X2_ASSETS")
    if not assets:
        assets = str(ROOT / "build/generated-assets/native")
        subprocess.run([sys.executable, str(ROOT / "tools/prepare_native_assets.py"),
                        game_value, assets], cwd=ROOT, check=True)
        os.environ["X2_ASSETS"] = assets
    build = build_directory()
    prepare_build_directory(build, toolchain)
    configure = [toolchain.cmake, "-S", str(ROOT), "-B", str(build), "-G", "Ninja",
                 f"-DCMAKE_MAKE_PROGRAM={toolchain.ninja}",
                 f"-DCMAKE_C_COMPILER={toolchain.cc}",
                 f"-DCMAKE_CXX_COMPILER={toolchain.cxx}",
                 f"-DPython3_EXECUTABLE={sys.executable}",
                 "-DCMAKE_BUILD_TYPE=RelWithDebInfo"]
    print(f"run: configuring x2native in {build} with {toolchain.cxx}")
    subprocess.run(configure, cwd=ROOT, check=True)
    subprocess.run([toolchain.cmake, "--build", str(build), "--target", "x2native",
                    "-j", str(os.cpu_count() or 1)], cwd=ROOT, check=True)
    executable = build / "x2native"
    if not executable.is_file():
        refuse(f"build completed but {executable} does not exist")
    print("run: NATIVE target; logging to scratch/logs/native.log")
    return tee_process([str(executable)], ROOT / "scratch/logs/native.log")


def main() -> int:
    if len(sys.argv) != 1:
        refuse("takes no arguments; invoke purpose-specific tools directly")
    os.chdir(ROOT)
    load_dotenv()
    try:
        return run_native()
    except subprocess.CalledProcessError as error:
        refuse(f"command failed with exit {error.returncode}: "
               + " ".join(str(part) for part in error.cmd))
    return 2


if __name__ == "__main__":
    raise SystemExit(main())
