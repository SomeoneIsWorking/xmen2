#!/usr/bin/env python3
"""Build a Linux AppImage from the already verified native release binary.

The AppImage contains the port, its native UI resources, and runtime libraries
discovered by linuxdeploy. It deliberately does not contain X-Men game files:
the player supplies a legally obtained install through the first-run picker.
"""

from __future__ import annotations

import argparse
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile


from ui_resource_files import UI_FILES

ROOT = Path(__file__).resolve().parent.parent
SCRATCH = ROOT / "scratch"
BUILD = ROOT / "build"
PACKAGING = ROOT / "packaging"
MINIMUM_PATCHELF_VERSION = (0, 19, 0)
# Ubuntu 22.04, the oldest LTS the AppImage catalog requires an AppImage to run on.
NEWEST_RUNTIME_VERSIONS = {"GLIBC": (2, 35), "GLIBCXX": (3, 4, 30), "CXXABI": (1, 3, 13)}
VERSION_NEED = re.compile(r"Name: (GLIBC|GLIBCXX|CXXABI)_(\d+(?:\.\d+)*)\b")


def refuse(message: str) -> None:
    raise SystemExit(f"appimage: {message}")


def require_file(path: Path, label: str) -> Path:
    if not path.is_file():
        refuse(f"{label} is missing: {path}")
    return path


def tool(name: str, configured: str | None) -> str:
    result = str(Path(configured).expanduser().resolve()) if configured else shutil.which(name)
    if not result:
        refuse(f"{name} is required; install it or set the corresponding option")
    if not Path(result).is_file():
        refuse(f"{name} is missing: {result}")
    return result


def parse_patchelf_version(text: str) -> tuple[int, ...] | None:
    match = re.search(r"\bpatchelf\s+(\d+(?:\.\d+)*)\b", text)
    return tuple(map(int, match.group(1).split("."))) if match else None


def modern_patchelf(patchelf: str) -> str:
    result = subprocess.run([patchelf, "--version"], text=True,
                            capture_output=True, check=False)
    detail = result.stdout.strip() or result.stderr.strip()
    version = parse_patchelf_version(detail)
    if result.returncode or version is None:
        refuse(f"could not identify patchelf at {patchelf}: {detail or 'no output'}")
    if version < MINIMUM_PATCHELF_VERSION:
        required = ".".join(map(str, MINIMUM_PATCHELF_VERSION))
        found = ".".join(map(str, version))
        refuse(f"patchelf {required}+ is required to preserve DT_INIT; found {found}")
    return patchelf


def copy_file(source: Path, destination: Path, label: str) -> None:
    require_file(source, label)
    destination.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(source, destination)


def linuxdeploy_environment() -> dict[str, str]:
    """Preserve modern ELF metadata that linuxdeploy's bundled strip cannot read.

    The deployer's own staged dependency scan remains authoritative; skipping
    its optional size optimisation avoids corrupting Fedora's RELR-bearing
    libraries before the package's runtime selftest can inspect them.
    """
    environment = dict(os.environ)
    environment["NO_STRIP"] = "1"
    return environment


def extracted_linuxdeploy(linuxdeploy: str, patchelf: str,
                          temporary: Path) -> str:
    """Use a fixed patcher inside the deployer's AppImage payload.

    linuxdeploy's bundled pre-0.19 patchelf corrupts DT_INIT when it rewrites
    current Fedora ELFs.  Its dependency scanner remains the deployment owner;
    only the known-bad ELF rewriter is replaced before that scan runs.
    """
    subprocess.run([linuxdeploy, "--appimage-extract"], cwd=temporary,
                   env=linuxdeploy_environment(), check=True)
    root = temporary / "squashfs-root"
    launcher = root / "AppRun"
    embedded_patchelf = root / "usr/bin/patchelf"
    if not launcher.is_file() or not embedded_patchelf.is_file():
        refuse(f"linuxdeploy did not extract an executable AppImage payload: {linuxdeploy}")
    copy_file(Path(patchelf), embedded_patchelf, "fixed patchelf")
    embedded_patchelf.chmod(0o755)
    return str(launcher)


def stage_appdir(appdir: Path, binary: Path, ui_directory: Path) -> None:
    """Create one complete AppDir; no game directory is an input here."""
    for relative in (
        "usr/bin/x2native",
        "usr/share/applications/xmen2-port.desktop",
        "usr/share/icons/hicolor/scalable/apps/xmen2-port.svg",
    ):
        (appdir / relative).parent.mkdir(parents=True, exist_ok=True)
    copy_file(binary, appdir / "usr/bin/x2native", "native release binary")
    copy_file(PACKAGING / "AppRun", appdir / "AppRun", "AppImage launcher")
    copy_file(PACKAGING / "xmen2-port.desktop",
              appdir / "usr/share/applications/xmen2-port.desktop",
              "desktop entry")
    copy_file(PACKAGING / "xmen2-port.svg",
              appdir / "usr/share/icons/hicolor/scalable/apps/xmen2-port.svg",
              "application icon")
    for name in UI_FILES:
        copy_file(ui_directory / name, appdir / "usr/share/xmen2" / name,
                  f"UI resource {name}")
    appdir.joinpath("AppRun").chmod(0o755)
    appdir.joinpath("usr/bin/x2native").chmod(0o755)


def verify_deployed_runtime(appdir: Path, working_directory: Path) -> None:
    """Reject a deployer that produced an ELF which dies before the runner starts.

    It launches through AppRun, as the packaged desktop entry does, so the
    checkout's `.env` is ignored. A missing player install is an expected
    setup-state exit (77).  A loader
    failure, signal, or failed selftest is not: it means the deployed library
    closure differs from the verified native binary.
    """
    result = subprocess.run([str(appdir / "AppRun"), "--no-window", "--selftest"],
                            cwd=working_directory,
                            text=True, capture_output=True, check=False)
    if result.returncode in (0, 77):
        return
    detail = result.stderr.strip() or result.stdout.strip() or "no diagnostic output"
    refuse("deployed runtime failed before release validation "
           f"(exit {result.returncode}): {detail}")


def runtime_versions_needed(readelf_output: str) -> set[tuple[str, tuple[int, ...]]]:
    return {(match[1], tuple(map(int, match[2].split("."))))
            for match in VERSION_NEED.finditer(readelf_output)}


def too_new(needed: set[tuple[str, tuple[int, ...]]]) -> list[str]:
    return sorted(f"{name}_{'.'.join(map(str, version))}" for name, version in needed
                  if version > NEWEST_RUNTIME_VERSIONS[name])


def verify_runtime_floor(appdir: Path) -> None:
    """Refuse an AppDir with an ELF that needs a newer C or C++ runtime than Ubuntu 22.04's."""
    readelf = tool("readelf", None)
    failures = []
    for path in sorted(appdir.rglob("*")):
        if path.is_symlink() or not path.is_file() or path.read_bytes()[:4] != b"\x7fELF":
            continue
        result = subprocess.run([readelf, "--version-info", "--wide", str(path)],
                                text=True, capture_output=True, check=True)
        newer = too_new(runtime_versions_needed(result.stdout))
        if newer:
            failures.append(f"{path.relative_to(appdir)} needs {', '.join(newer)}")
    if failures:
        refuse("built on a host newer than Ubuntu 22.04; build it with "
               "tools/appimage_container.py:\n  " + "\n  ".join(failures))


def build_appimage(binary: Path, ui_directory: Path, output: Path,
                   linuxdeploy: str, appimagetool: str, patchelf: str) -> None:
    raw = SCRATCH / "raw"
    raw.mkdir(parents=True, exist_ok=True)
    temporary = Path(tempfile.mkdtemp(prefix="xmen2-appdir-", dir=raw))
    appdir = temporary / "AppDir"
    try:
        stage_appdir(appdir, binary, ui_directory)
        deployer = extracted_linuxdeploy(linuxdeploy, patchelf, temporary)
        subprocess.run([
            deployer,
            "--appdir", str(appdir),
            "--executable", str(appdir / "usr/bin/x2native"),
        ], cwd=temporary, env=linuxdeploy_environment(), check=True)
        verify_deployed_runtime(appdir, temporary)
        verify_runtime_floor(appdir)
        output.parent.mkdir(parents=True, exist_ok=True)
        subprocess.run([appimagetool, str(appdir), str(output)],
                       cwd=temporary,
                       env=dict(os.environ, APPIMAGE_EXTRACT_AND_RUN="1"),
                       check=True)
    finally:
        shutil.rmtree(temporary)
    if not output.is_file() or output.stat().st_size == 0:
        refuse(f"appimagetool returned without creating {output}")
    output.chmod(0o755)
    print(f"appimage: created {output} ({output.stat().st_size} bytes)")


def selftest() -> int:
    raw = SCRATCH / "raw"
    raw.mkdir(parents=True, exist_ok=True)
    temporary = Path(tempfile.mkdtemp(prefix="xmen2-appimage-selftest-", dir=raw))
    try:
        binary = temporary / "x2native"
        ui = temporary / "ui"
        ui.mkdir()
        binary.write_bytes(b"native fixture")
        for name in UI_FILES:
            (ui / name).write_bytes(name.encode())
        appdir = temporary / "AppDir"
        stage_appdir(appdir, binary, ui)
        required = [
            appdir / "AppRun",
            appdir / "usr/bin/x2native",
            appdir / "usr/share/applications/xmen2-port.desktop",
            appdir / "usr/share/icons/hicolor/scalable/apps/xmen2-port.svg",
            appdir / "usr/share/xmen2/touch_controls.rcss",
        ]
        complete = all(path.is_file() for path in required)
        no_game = not any(path.name.lower() == "xmen2.exe"
                          for path in appdir.rglob("*"))
        launcher = (appdir / "AppRun").read_text()
        portable = "X2_UI_RESOURCE_DIR" in launcher and "--appimage" in launcher
        relative_tool = tool("fixture", os.path.relpath(binary, Path.cwd()))
        tool_is_absolute = Path(relative_tool).is_absolute() and Path(relative_tool) == binary
        preserves_modern_elf = linuxdeploy_environment()["NO_STRIP"] == "1"
        version_parse = (parse_patchelf_version("patchelf 0.19.1") ==
                         (0, 19, 1))
        old_patcher_refused = (parse_patchelf_version("patchelf 0.18.0") <
                               MINIMUM_PATCHELF_VERSION)
        floor = (too_new(runtime_versions_needed(
            "  0x0010: Name: GLIBC_2.34  Flags: none\n  0x0020: Name: GLIBCXX_3.4.30\n")) == []
            and too_new(runtime_versions_needed("Name: GLIBC_2.38\n")) == ["GLIBC_2.38"])
        print("package_appimage selftest: "
              f"complete={complete} no-game-files={no_game} portable-resources={portable} "
              f"absolute-tools={tool_is_absolute} modern-elf={preserves_modern_elf} "
              f"patchelf-version={version_parse and old_patcher_refused} runtime-floor={floor}")
        return 0 if (complete and no_game and portable and tool_is_absolute and
                     preserves_modern_elf and version_parse and old_patcher_refused
                     and floor) else 1
    finally:
        shutil.rmtree(temporary)


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--selftest", action="store_true")
    parser.add_argument("--build-dir", type=Path, default=BUILD / "native")
    parser.add_argument("--output", type=Path,
                        default=BUILD / "release" / "X-Men-Legends-II-x86_64.AppImage")
    parser.add_argument("--linuxdeploy")
    parser.add_argument("--appimagetool")
    parser.add_argument("--patchelf")
    args = parser.parse_args(argv)
    if args.selftest:
        return selftest()
    binary = require_file(args.build_dir / "x2native", "build output")
    ui = args.build_dir / "ui"
    linuxdeploy = tool("linuxdeploy", args.linuxdeploy)
    appimagetool = tool("appimagetool", args.appimagetool)
    patchelf = modern_patchelf(tool("patchelf", args.patchelf))
    build_appimage(binary, ui, args.output.resolve(), linuxdeploy, appimagetool, patchelf)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
