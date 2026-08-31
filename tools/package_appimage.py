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
import shutil
import subprocess
import tempfile


ROOT = Path(__file__).resolve().parent.parent
SCRATCH = ROOT / "scratch"
BUILD = ROOT / "build"
PACKAGING = ROOT / "packaging"


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


def copy_file(source: Path, destination: Path, label: str) -> None:
    require_file(source, label)
    destination.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(source, destination)


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
    for name in ("LatoLatin-Regular.ttf", "LatoLatin-Bold.ttf", "settings.rcss",
                 "touch_controls.rcss"):
        copy_file(ui_directory / name, appdir / "usr/share/xmen2" / name,
                  f"UI resource {name}")
    touch_directory = ui_directory / "touch"
    touch_icons = sorted(touch_directory.glob("*.svg"))
    if not touch_icons:
        refuse(f"touch-control SVG resources are missing: {touch_directory}")
    for icon in touch_icons:
        copy_file(icon, appdir / "usr/share/xmen2/touch" / icon.name,
                  f"touch-control icon {icon.name}")
    appdir.joinpath("AppRun").chmod(0o755)
    appdir.joinpath("usr/bin/x2native").chmod(0o755)


def verify_deployed_runtime(appdir: Path, working_directory: Path) -> None:
    """Reject a deployer that produced an ELF which dies before the runner starts.

    A missing player install is an expected setup-state exit (77).  A loader
    failure, signal, or failed selftest is not: it means the deployed library
    closure differs from the verified native binary.
    """
    binary = appdir / "usr/bin/x2native"
    environment = dict(os.environ)
    environment["X2_UI_RESOURCE_DIR"] = str(appdir / "usr/share/xmen2")
    result = subprocess.run([str(binary), "--no-window", "--selftest"],
                            cwd=working_directory, env=environment,
                            text=True, capture_output=True, check=False)
    if result.returncode in (0, 77):
        return
    detail = result.stderr.strip() or result.stdout.strip() or "no diagnostic output"
    refuse("deployed runtime failed before release validation "
           f"(exit {result.returncode}): {detail}")


def build_appimage(binary: Path, ui_directory: Path, output: Path,
                   linuxdeploy: str, appimagetool: str) -> None:
    raw = SCRATCH / "raw"
    raw.mkdir(parents=True, exist_ok=True)
    temporary = Path(tempfile.mkdtemp(prefix="xmen2-appdir-", dir=raw))
    appdir = temporary / "AppDir"
    try:
        stage_appdir(appdir, binary, ui_directory)
        subprocess.run([
            linuxdeploy,
            "--appdir", str(appdir),
            "--executable", str(appdir / "usr/bin/x2native"),
        ], cwd=temporary, check=True)
        verify_deployed_runtime(appdir, temporary)
        output.parent.mkdir(parents=True, exist_ok=True)
        subprocess.run([appimagetool, str(appdir), str(output)],
                       cwd=temporary, check=True)
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
        for name in ("LatoLatin-Regular.ttf", "LatoLatin-Bold.ttf", "settings.rcss",
                     "touch_controls.rcss"):
            (ui / name).write_bytes(name.encode())
        touch = ui / "touch"
        touch.mkdir()
        (touch / "face_a.svg").write_text("<svg/>", encoding="ascii")
        appdir = temporary / "AppDir"
        stage_appdir(appdir, binary, ui)
        required = [
            appdir / "AppRun",
            appdir / "usr/bin/x2native",
            appdir / "usr/share/applications/xmen2-port.desktop",
            appdir / "usr/share/icons/hicolor/scalable/apps/xmen2-port.svg",
            appdir / "usr/share/xmen2/touch_controls.rcss",
            appdir / "usr/share/xmen2/touch/face_a.svg",
        ]
        complete = all(path.is_file() for path in required)
        no_game = not any(path.name.lower() == "xmen2.exe"
                          for path in appdir.rglob("*"))
        launcher = (appdir / "AppRun").read_text()
        portable = "X2_UI_RESOURCE_DIR" in launcher and "--appimage" in launcher
        relative_tool = tool("fixture", os.path.relpath(binary, Path.cwd()))
        tool_is_absolute = Path(relative_tool).is_absolute() and Path(relative_tool) == binary
        print("package_appimage selftest: "
              f"complete={complete} no-game-files={no_game} portable-resources={portable} "
              f"absolute-tools={tool_is_absolute}")
        return 0 if complete and no_game and portable and tool_is_absolute else 1
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
    args = parser.parse_args(argv)
    if args.selftest:
        return selftest()
    binary = require_file(args.build_dir / "x2native", "build output")
    ui = args.build_dir / "ui"
    linuxdeploy = tool("linuxdeploy", args.linuxdeploy)
    appimagetool = tool("appimagetool", args.appimagetool)
    build_appimage(binary, ui, args.output.resolve(), linuxdeploy, appimagetool)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
