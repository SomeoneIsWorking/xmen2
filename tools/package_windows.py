#!/usr/bin/env python3
"""Zip the cross-built Windows native binary into a portable folder.

    python3 tools/package_windows.py --build-dir build/windows-x86_64
    python3 tools/package_windows.py --selftest

The ZIP holds one folder: `X-Men Legends II.exe`, the UI resources next to it
and the marker file that tells the executable it is the packaged product (see
src/native/windows_package.h). The build links SDL3, SDL3_image, FreeType, zlib
and FFmpeg statically, so no DLL ships; the packager reads the executable's
import table and refuses any import that is not a Windows system library. It
deliberately contains no X-Men game files: the player picks a legally obtained
install in the first-run picker, which the executable enters on its own.
"""

from __future__ import annotations

import argparse
import re
import shutil
import subprocess
import sys
import tempfile
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))

import windows_deps  # noqa: E402
from ui_resource_files import UI_FILES  # noqa: E402

SCRATCH = ROOT / "scratch"
BUILD = ROOT / "build"
PRODUCT = "X-Men Legends II"
MARKER = "xmen2-package.txt"  # kWindowsPackageMarker in windows_package.h
ZIP_NAME = "X-Men-Legends-II-windows-x86_64.zip"

# Every library the executable may import, all part of Windows itself. The
# api-ms-win-crt-* set is the Universal CRT, present on Windows 10 and later.
SYSTEM_DLLS = frozenset(name.lower() for name in (
    "ADVAPI32.dll", "bcrypt.dll", "GDI32.dll", "IMM32.dll", "IPHLPAPI.DLL",
    "KERNEL32.dll", "ole32.dll", "OLEAUT32.dll", "SETUPAPI.dll", "SHELL32.dll",
    "USER32.dll", "VERSION.dll", "WINMM.dll", "WS2_32.dll",
))
UNIVERSAL_CRT = re.compile(r"api-ms-win-crt-[a-z0-9-]+-l\d+-\d+-\d+\.dll", re.I)

README = f"""{PRODUCT} native port (Windows x86-64)

Run "{PRODUCT}.exe". The first launch asks for your own PC installation of
X-Men Legends II: a folder, its XMen2.exe, or a ZIP of either. The game's files
are not part of this package. Windows 10 or later is required.

Keep this file and the ui folder next to the executable: their presence is how
the program knows it is running as the packaged product.
"""


def refuse(message: str) -> None:
    raise SystemExit(f"windows: {message}")


def require_file(path: Path, label: str) -> Path:
    if not path.is_file():
        refuse(f"{label} is missing: {path}")
    return path


def imported_dlls(objdump_output: str) -> list[str]:
    return sorted({match.strip() for match in
                   re.findall(r"DLL Name:\s*(\S+)", objdump_output)}, key=str.lower)


def foreign_imports(dlls: list[str]) -> list[str]:
    return [name for name in dlls
            if name.lower() not in SYSTEM_DLLS and not UNIVERSAL_CRT.fullmatch(name)]


def verify_imports(binary: Path, objdump: str) -> list[str]:
    result = subprocess.run([objdump, "-p", str(binary)], text=True,
                            capture_output=True, check=False)
    if result.returncode:
        refuse(f"{objdump} -p failed on {binary}: {result.stderr.strip()}")
    dlls = imported_dlls(result.stdout)
    if not dlls:
        refuse(f"{binary} has no import table; is it a Windows executable?")
    foreign = foreign_imports(dlls)
    if foreign:
        refuse(f"{binary.name} imports non-system libraries {foreign}; "
               "they must be linked statically or shipped")
    return dlls


def stage_folder(folder: Path, binary: Path, ui_directory: Path) -> None:
    """Create the complete package folder; no game directory is an input."""
    (folder / "ui").mkdir(parents=True)
    shutil.copy2(require_file(binary, "native release binary"),
                 folder / f"{PRODUCT}.exe")
    for name in UI_FILES:
        shutil.copy2(require_file(ui_directory / name, f"UI resource {name}"),
                     folder / "ui" / name)
    (folder / MARKER).write_text(README, encoding="utf-8", newline="\r\n")
    license_file = ROOT / "LICENSE"
    if license_file.is_file():
        shutil.copy2(license_file, folder / "LICENSE.txt")


def write_zip(folder: Path, output: Path) -> None:
    """Zip `folder` as its own top-level directory, reproducibly."""
    output.parent.mkdir(parents=True, exist_ok=True)
    output.unlink(missing_ok=True)
    with zipfile.ZipFile(output, "w", zipfile.ZIP_DEFLATED, compresslevel=9) as archive:
        for path in sorted(folder.rglob("*")):
            if not path.is_file():
                continue
            entry = zipfile.ZipInfo(f"{folder.name}/{path.relative_to(folder).as_posix()}",
                                    date_time=(2005, 1, 1, 0, 0, 0))
            entry.compress_type = zipfile.ZIP_DEFLATED
            entry.external_attr = 0o644 << 16
            archive.writestr(entry, path.read_bytes())


def build(binary: Path, ui_directory: Path, output: Path, objdump: str) -> None:
    dlls = verify_imports(binary, objdump)
    raw = SCRATCH / "raw"
    raw.mkdir(parents=True, exist_ok=True)
    temporary = Path(tempfile.mkdtemp(prefix="xmen2-windows-", dir=raw))
    try:
        folder = temporary / PRODUCT
        stage_folder(folder, binary, ui_directory)
        write_zip(folder, output)
    finally:
        shutil.rmtree(temporary)
    print(f"windows: created {output} ({output.stat().st_size} bytes); "
          f"imports only {len(dlls)} system libraries")


def selftest() -> int:
    raw = SCRATCH / "raw"
    raw.mkdir(parents=True, exist_ok=True)
    temporary = Path(tempfile.mkdtemp(prefix="xmen2-windows-selftest-", dir=raw))
    try:
        binary = temporary / "x2native.exe"
        binary.write_bytes(b"native fixture")
        ui = temporary / "ui"
        ui.mkdir()
        for name in UI_FILES:
            (ui / name).write_bytes(name.encode())
        folder = temporary / PRODUCT
        stage_folder(folder, binary, ui)
        output = temporary / ZIP_NAME
        write_zip(folder, output)
        with zipfile.ZipFile(output) as archive:
            names = set(archive.namelist())
        expected = {f"{PRODUCT}/{PRODUCT}.exe", f"{PRODUCT}/{MARKER}",
                    *(f"{PRODUCT}/ui/{name}" for name in UI_FILES)}
        complete = expected <= names
        no_game = not any(Path(name).name.lower() == "xmen2.exe" for name in names)
        one_folder = all(name.startswith(f"{PRODUCT}/") for name in names)
        dump = "DLL Name: KERNEL32.dll\nDLL Name: api-ms-win-crt-stdio-l1-1-0.dll\n"
        system_only = not foreign_imports(imported_dlls(dump))
        refuses_sdl = foreign_imports(["SDL3.dll", "KERNEL32.dll"]) == ["SDL3.dll"]
        print(f"package_windows selftest: complete={complete} no-game-files={no_game} "
              f"one-folder={one_folder} system-imports={system_only} "
              f"foreign-import-refused={refuses_sdl}")
        return 0 if (complete and no_game and one_folder and system_only
                     and refuses_sdl) else 1
    finally:
        shutil.rmtree(temporary)


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--selftest", action="store_true")
    parser.add_argument("--build-dir", type=Path, default=BUILD / "windows-x86_64")
    parser.add_argument("--deps-root", type=Path, default=windows_deps.DEFAULT_DEPS_ROOT)
    parser.add_argument("--objdump", help="llvm-objdump (default: the pinned llvm-mingw's)")
    parser.add_argument("--output", type=Path, default=BUILD / "release" / ZIP_NAME)
    args = parser.parse_args(argv)
    if args.selftest:
        return selftest()
    tool = "llvm-objdump.exe" if sys.platform == "win32" else "llvm-objdump"
    objdump = args.objdump or str(
        windows_deps.llvm_mingw_root(args.deps_root.resolve()) / "bin" / tool)
    build_dir = args.build_dir.resolve()
    build(build_dir / "x2native.exe", build_dir / "ui", args.output.resolve(), objdump)
    return 0


if __name__ == "__main__":
    sys.exit(main())
