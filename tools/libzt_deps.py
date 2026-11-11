"""Provision libzt, the embedded ZeroTier node `lucent::zerotier` links.

libzt is built static from pinned archives (the fork and its three submodule
trees) once per revision and toolchain, into a shared prefix outside the repo
that every consumer finds through `CMAKE_PREFIX_PATH`. The prefix is named
`libzt-<revision>-<toolchain>`, as Lucent's own agent tree names it.
"""

from __future__ import annotations

import os
from pathlib import Path
import shutil
import subprocess
from typing import Sequence

from dependency_archives import fetch, github_commit, refuse, run, unpack


ROOT = Path(__file__).resolve().parents[1]
SOURCES = ROOT / "build" / "deps" / "sources"

LIBZT = github_commit("SomeoneIsWorking", "libzt", "9932062b4b55e91eb08ced6edc4e282a2c462f74",
                      "ad911c4f8b732d5aa4edcd6c712bb7a0720ab0b339dd6db1ecc09fa5f2610a5a")
SUBMODULES = (
    ("ext/ZeroTierOne", github_commit(
        "SomeoneIsWorking", "ZeroTierOne", "23c013918e6b5fd4910652605196a8485cd647d4",
        "a0d64f419a7d5dbd4a7f423ebaa797f88c2145c67645faf7013e906a739b8acc")),
    ("ext/lwip", github_commit(
        "joseph-henry", "lwip", "32708c0a8b140efb545cc35101ee5fdeca6d6489",
        "49e5a5a1c5b4eedcc749abd60e839d4552618fbbe72a6920447e7dcacb3ea4bf")),
    ("ext/lwip-contrib", github_commit(
        "joseph-henry", "lwip-contrib", "4fd612c9c72dfcd1db6618bd59c1a17d9f5b55f8",
        "8d0f2c9c54dc2f51e213afc52ebc3be1615a2269d4aa0f93198de86ac67178f3")),
)
OPTIONS = ("-DBUILD_STATIC_LIB=ON", "-DBUILD_SHARED_LIB=OFF", "-DBUILD_HOST_SELFTEST=OFF",
           "-DCMAKE_BUILD_TYPE=Release", "-DCMAKE_POSITION_INDEPENDENT_CODE=ON",
           "-DCMAKE_INSTALL_LIBDIR=lib")


def shared_root() -> Path:
    data = os.environ.get("XDG_DATA_HOME")
    return (Path(data) if data else Path.home() / ".local" / "share") / "lucent-thirdparty"


def prefix_for(toolchain: str) -> Path:
    return shared_root() / f"libzt-{LIBZT.version[:7]}-{toolchain}"


def installed(prefix: Path) -> bool:
    return ((prefix / "lib" / "cmake" / "libzt" / "libztConfig.cmake").is_file()
            and (prefix / "include" / "ZeroTierSockets.h").is_file())


def source_tree(work: Path) -> Path:
    tree = unpack(fetch(LIBZT, SOURCES), work / "src")
    for path, archive in SUBMODULES:
        destination = tree / path
        if destination.is_dir() and any(destination.iterdir()):
            continue
        if destination.is_dir():
            destination.rmdir()
        unpack(fetch(archive, SOURCES), work / "src").rename(destination)
    return tree


def provision(toolchain: str, cmake_options: Sequence[str], jobs: int,
              environment: dict[str, str] | None = None) -> Path:
    """Return the prefix for `toolchain`, building libzt into it first if it is missing."""
    prefix = prefix_for(toolchain)
    if installed(prefix):
        return prefix
    work = prefix.with_name(prefix.name + ".work")
    stage = prefix.with_name(prefix.name + ".stage")
    for path in (work, stage):
        if path.exists():
            shutil.rmtree(path)
    tree = source_tree(work)
    build = work / "build"
    run(["cmake", "-S", str(tree), "-B", str(build), "-G", "Ninja", *OPTIONS,
         f"-DCMAKE_INSTALL_PREFIX={stage}", *cmake_options], env=environment)
    run(["cmake", "--build", str(build), "--parallel", str(jobs)], env=environment)
    run(["cmake", "--install", str(build)], env=environment)
    if not installed(stage):
        refuse(f"the libzt build did not install its CMake package into {stage}")
    if prefix.exists():
        shutil.rmtree(prefix)
    stage.rename(prefix)
    shutil.rmtree(work)
    return prefix


def host_prefix(cc: str, cxx: str, jobs: int, make_program: str | None = None) -> Path:
    """The prefix for a host compiler pair, named by its family and version (`clang22.1.8`)."""
    version = subprocess.run([cxx, "-dumpversion"], check=True, capture_output=True,
                             text=True).stdout.strip()
    family = "clang" if "clang" in Path(cxx).name.casefold() else "gnu"
    options = [f"-DCMAKE_C_COMPILER={cc}", f"-DCMAKE_CXX_COMPILER={cxx}"]
    if make_program:
        options.append(f"-DCMAKE_MAKE_PROGRAM={make_program}")
    return provision(f"{family}{version}", options, jobs)
