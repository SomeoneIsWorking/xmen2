#!/usr/bin/env python3
"""Build a self-contained macOS .app from the verified native release binary.

    python3 tools/package_macos.py --output build/release/X-Men Legends II.app
    python3 tools/package_macos.py --selftest

The bundle contains the port, its UI resources, every non-system library the
binary links, and the Vulkan loader with MoltenVK that SDL_GPU needs on macOS
(the renderer is driven from SPIR-V, so Metal is reached through MoltenVK, and
neither piece is part of the OS). It deliberately does not contain X-Men game
files: the player supplies a legally obtained install -- a folder, an
`XMen2.exe` inside one, or a ZIP -- through the first-run picker, which the
executable enters on its own because it can see it is inside a bundle.

The dylib closure is walked from `otool -L` and rewritten to `@rpath`, and the
result is ad-hoc signed: on Apple Silicon a Mach-O whose load commands were
edited will not run until it is signed again, so an unsigned bundle here is a
bundle that crashes on the user's machine rather than one that merely lacks a
developer identity.
"""

from __future__ import annotations

import argparse
import os
from pathlib import Path
import plistlib
import shutil
import subprocess
import tempfile


ROOT = Path(__file__).resolve().parent.parent
SCRATCH = ROOT / "scratch"
BUILD = ROOT / "build"
PACKAGING = ROOT / "packaging"

BUNDLE_ID = "ch.cdi.xmen2.port"
BUNDLE_NAME = "X-Men Legends II"

# Everything under these prefixes belongs to macOS and must NOT be copied: it
# is present on every machine, and a bundled copy of a system library is the
# reliable way to get two incompatible copies loaded at once.
SYSTEM_PREFIXES = ("/usr/lib/", "/System/", "/Library/Apple/")

UI_FILES = ("LatoLatin-Regular.ttf", "LatoLatin-Bold.ttf", "settings.rcss",
            "touch_controls.rcss")
UI_DIRECTORIES = ("touch", "icons")


def refuse(message: str) -> None:
    raise SystemExit(f"macos: {message}")


def require_file(path: Path, label: str) -> Path:
    if not path.is_file():
        refuse(f"{label} is missing: {path}")
    return path


def run(command: list[str]) -> str:
    result = subprocess.run(command, text=True, capture_output=True, check=False)
    if result.returncode:
        detail = result.stderr.strip() or result.stdout.strip() or "no output"
        refuse(f"command failed ({result.returncode}): {' '.join(command)}\n  {detail}")
    return result.stdout


def bundled_library(name: str) -> bool:
    """Whether one install name is a library this bundle has to carry.

    System libraries are on every machine and a second copy of one loaded
    beside the real one is its own failure. Everything else is carried,
    INCLUDING the `@rpath/...` names Homebrew libraries use for each other:
    those resolve today only because the build tree's own rpath is still in
    the binary, and stripping that (see build_tree_rpaths) is exactly what
    makes the bundle self-contained.
    """
    return bool(name) and not name.startswith(SYSTEM_PREFIXES)


def all_rpaths(target: Path) -> list[str]:
    """Every LC_RPATH entry of one Mach-O, in load order."""
    lines = run(["otool", "-l", str(target)]).splitlines()
    paths = []
    for index, line in enumerate(lines):
        if line.strip() != "cmd LC_RPATH":
            continue
        for following in lines[index:index + 4]:
            value = following.strip()
            if value.startswith("path "):
                paths.append(value[5:].rsplit(" (offset", 1)[0].strip())
                break
    return paths


def build_tree_rpaths(target: Path) -> list[str]:
    """The LC_RPATH entries that point OUTSIDE the bundle.

    The CMake build bakes the machine's Homebrew lib directory in, and it is
    searched BEFORE anything added later: leaving it in place makes every
    `@rpath/...` resolve to the packager's Homebrew on a machine that has one,
    so the bundle would be self-contained only on machines that do not have
    it. Measured on 2026-09-06: with /opt/homebrew/lib still first, a fully
    staged bundle loaded 37 Homebrew libraries and one of its own.
    """
    return [path for path in all_rpaths(target) if not path.startswith("@")]


def resolve(name: str, owner: Path) -> Path | None:
    """The file one install name refers to, as `owner` would resolve it."""
    if name.startswith("@loader_path/"):
        return (owner.parent / name[len("@loader_path/"):]).resolve()
    if name.startswith("@executable_path/"):
        return None  # only meaningful once staged; the staged copy is the file
    if name.startswith("@rpath/"):
        leaf = name[len("@rpath/"):]
        for base in all_rpaths(owner):
            candidate = (owner.parent / leaf) if base.startswith("@") \
                else Path(base) / leaf
            if candidate.is_file():
                return candidate.resolve()
        return None
    return Path(name)


def linked_libraries(binary: Path) -> list[str]:
    """The install names a Mach-O asks for, minus the ones it must not carry."""
    return [name for name in
            (line.strip().split(" (", 1)[0].strip()
             for line in run(["otool", "-L", str(binary)]).splitlines()[1:])
            if bundled_library(name)]


def copy_closure(binary: Path, frameworks: Path) -> dict[str, str]:
    """Copy every non-system library the binary needs, transitively.

    Returns install name -> bundled file name for the ABSOLUTE names, which is
    what the rewrite pass needs; an `@rpath/...` name already points at the
    bundle once its file is staged beside the others, so it is copied and left
    alone.

    Every name is resolved against the library's ORIGINAL location, never the
    staged copy: a Homebrew dylib carries rpaths relative to where it was
    installed, and asking the copy in Frameworks to resolve them finds
    nothing.
    """
    staged: dict[str, str] = {}
    seen: set[str] = set()
    pending = [(name, binary) for name in linked_libraries(binary)]
    while pending:
        name, owner = pending.pop()
        key = f"{owner}|{name}"
        if key in seen:
            continue
        seen.add(key)
        source = resolve(name, owner)
        if source is None or not source.is_file():
            refuse(f"{owner.name} needs {name}, which is not on this machine")
        destination = frameworks / Path(name).name
        if not name.startswith("@"):
            staged[name] = destination.name
        if not destination.exists():
            shutil.copy2(source, destination)
            destination.chmod(0o755)
        pending.extend((further, source)
                       for further in linked_libraries(source))
    return staged


def rewrite(target: Path, staged: dict[str, str], is_binary: bool) -> None:
    """Point one Mach-O at the bundled copies and at nothing outside them."""
    for name, bundled in staged.items():
        run(["install_name_tool", "-change", name, f"@rpath/{bundled}",
             str(target)])
    for path in build_tree_rpaths(target):
        run(["install_name_tool", "-delete_rpath", path, str(target)])
    if is_binary:
        run(["install_name_tool", "-add_rpath", "@executable_path/../Frameworks",
             str(target)])
    else:
        run(["install_name_tool", "-id", f"@rpath/{target.name}", str(target)])
        run(["install_name_tool", "-add_rpath", "@loader_path", str(target)])


def homebrew_prefix() -> Path:
    prefix = Path(run(["brew", "--prefix"]).strip())
    if not prefix.is_dir():
        refuse(f"brew --prefix is not a directory: {prefix}")
    return prefix


def stage_vulkan(frameworks: Path, resources: Path) -> None:
    """The loader, MoltenVK, and a driver manifest that points inside the app.

    The shipped manifest is written here rather than copied: Homebrew's own
    names an absolute /opt path, which resolves to the packager's machine on
    every machine that is not the packager's.
    """
    prefix = homebrew_prefix()
    loader = require_file(prefix / "lib/libvulkan.1.dylib", "the Vulkan loader")
    driver = require_file(prefix / "lib/libMoltenVK.dylib", "MoltenVK")
    for source in (loader, driver):
        destination = frameworks / source.name
        shutil.copy2(source, destination)
        destination.chmod(0o755)
    icd = resources / "vulkan/icd.d"
    icd.mkdir(parents=True, exist_ok=True)
    (icd / "MoltenVK_icd.json").write_text(
        '{\n'
        '  "file_format_version": "1.0.0",\n'
        '  "ICD": {\n'
        '    "library_path": "../../../Frameworks/libMoltenVK.dylib",\n'
        '    "api_version": "1.2.0",\n'
        '    "is_portability_driver": true\n'
        '  }\n'
        '}\n', encoding="ascii")


def info_plist(executable: str) -> bytes:
    return plistlib.dumps({
        "CFBundleName": BUNDLE_NAME,
        "CFBundleDisplayName": BUNDLE_NAME,
        "CFBundleIdentifier": BUNDLE_ID,
        "CFBundleExecutable": executable,
        "CFBundlePackageType": "APPL",
        "CFBundleShortVersionString": "0.1.4",
        "CFBundleVersion": "0.1.4",
        "CFBundleIconFile": "AppIcon",
        "LSMinimumSystemVersion": "13.0",
        "LSApplicationCategoryType": "public.app-category.games",
        "NSHighResolutionCapable": True,
        # The picker opens a file dialog over the user's own disk; macOS shows
        # this string when the folder they choose is a protected one.
        "NSDesktopFolderUsageDescription":
            "X-Men Legends II needs to read the game installation you choose.",
        "NSDocumentsFolderUsageDescription":
            "X-Men Legends II needs to read the game installation you choose.",
        "NSDownloadsFolderUsageDescription":
            "X-Men Legends II needs to read the game installation you choose.",
        "NSRemovableVolumesUsageDescription":
            "X-Men Legends II needs to read the game installation you choose.",
    })


def stage_ui(ui_directory: Path, resources: Path) -> None:
    target = resources / "ui"
    target.mkdir(parents=True, exist_ok=True)
    for name in UI_FILES:
        shutil.copy2(require_file(ui_directory / name, f"UI resource {name}"),
                     target / name)
    for name in UI_DIRECTORIES:
        source = ui_directory / name
        if not source.is_dir():
            continue
        (target / name).mkdir(exist_ok=True)
        for svg in sorted(source.glob("*.svg")):
            shutil.copy2(svg, target / name / svg.name)
    if not sorted((target / "touch").glob("*.svg")):
        refuse(f"touch-control SVG resources are missing: {ui_directory / 'touch'}")


def stage_icon(resources: Path) -> None:
    """Render the shipped SVG into an .icns, or leave the bundle iconless.

    An iconless app still runs; refusing the whole package because a rasteriser
    is absent would trade the product for its picture.
    """
    source = PACKAGING / "xmen2-port.svg"
    if not source.is_file() or not shutil.which("iconutil"):
        return
    try:
        from resvg_py import svg_to_bytes
    except ImportError:
        return
    with tempfile.TemporaryDirectory(prefix="xmen2-icns-") as raw:
        iconset = Path(raw) / "AppIcon.iconset"
        iconset.mkdir()
        for size in (16, 32, 64, 128, 256, 512, 1024):
            png = svg_to_bytes(svg_path=str(source), width=size, height=size)
            (iconset / f"icon_{size}x{size}.png").write_bytes(bytes(png))
            if size >= 32:
                (iconset / f"icon_{size // 2}x{size // 2}@2x.png").write_bytes(
                    bytes(png))
        run(["iconutil", "-c", "icns", str(iconset),
             "-o", str(resources / "AppIcon.icns")])


def stage_bundle(app: Path, binary: Path, ui_directory: Path,
                 with_libraries: bool = True) -> None:
    """Create one complete .app; no game directory is an input here."""
    contents = app / "Contents"
    macos = contents / "MacOS"
    resources = contents / "Resources"
    frameworks = contents / "Frameworks"
    for directory in (macos, resources, frameworks):
        directory.mkdir(parents=True, exist_ok=True)
    executable = macos / "x2native"
    shutil.copy2(require_file(binary, "native release binary"), executable)
    executable.chmod(0o755)
    (contents / "Info.plist").write_bytes(info_plist(executable.name))
    (contents / "PkgInfo").write_text("APPL????", encoding="ascii")
    stage_ui(ui_directory, resources)
    for name in ("README.md", "LICENSE"):
        source = ROOT / name
        if source.is_file():
            shutil.copy2(source, resources / name)
    if not with_libraries:
        return
    staged = copy_closure(executable, frameworks)
    stage_vulkan(frameworks, resources)
    rewrite(executable, staged, is_binary=True)
    for library in sorted(frameworks.glob("*.dylib")):
        rewrite(library, staged, is_binary=False)
    stage_icon(resources)
    # Ad-hoc, last: every load command must already be final, because signing
    # covers them and install_name_tool would invalidate the signature.
    run(["codesign", "--force", "--deep", "--sign", "-", str(app)])
    run(["codesign", "--verify", "--deep", str(app)])


def verify_bundle(app: Path) -> None:
    """Reject a bundle whose runtime dies before the game could be reached.

    A missing player install is the expected setup-state exit (77). A loader
    failure or a signal is not: it means the staged library closure differs
    from the binary that was verified.
    """
    executable = app / "Contents/MacOS/x2native"
    environment = dict(os.environ)
    environment.pop("GAME_PC_DIR", None)
    result = subprocess.run([str(executable), "--no-window", "--selftest"],
                            env=environment, text=True, capture_output=True,
                            check=False)
    if result.returncode in (0, 77):
        return
    detail = result.stderr.strip() or result.stdout.strip() or "no diagnostic output"
    refuse("the bundled runtime failed before release validation "
           f"(exit {result.returncode}): {detail}")


def build(binary: Path, ui_directory: Path, output: Path) -> None:
    if output.exists():
        shutil.rmtree(output)
    output.parent.mkdir(parents=True, exist_ok=True)
    stage_bundle(output, binary, ui_directory)
    verify_bundle(output)
    print(f"macos: created {output}")


def selftest() -> int:
    raw = SCRATCH / "raw"
    raw.mkdir(parents=True, exist_ok=True)
    temporary = Path(tempfile.mkdtemp(prefix="xmen2-macos-selftest-", dir=raw))
    try:
        binary = temporary / "x2native"
        binary.write_bytes(b"native fixture")
        ui = temporary / "ui"
        ui.mkdir()
        for name in UI_FILES:
            (ui / name).write_bytes(name.encode())
        (ui / "touch").mkdir()
        (ui / "touch/face_a.svg").write_text("<svg/>", encoding="ascii")
        (ui / "icons").mkdir()
        (ui / "icons/attack.svg").write_text("<svg/>", encoding="ascii")
        app = temporary / "X-Men Legends II.app"
        stage_bundle(app, binary, ui, with_libraries=False)
        required = [
            app / "Contents/Info.plist",
            app / "Contents/MacOS/x2native",
            app / "Contents/Resources/ui/settings.rcss",
            app / "Contents/Resources/ui/touch/face_a.svg",
            app / "Contents/Resources/ui/icons/attack.svg",
        ]
        complete = all(path.is_file() for path in required)
        no_game = not any(path.name.lower() == "xmen2.exe"
                          for path in app.rglob("*"))
        plist = plistlib.loads((app / "Contents/Info.plist").read_bytes())
        launches = (plist["CFBundleExecutable"] == "x2native"
                    and plist["CFBundleIdentifier"] == BUNDLE_ID)
        filtered = (not bundled_library("/usr/lib/libSystem.B.dylib")
                    and not bundled_library("/System/Library/Frameworks/A")
                    and not bundled_library("@rpath/libSDL3.0.dylib")
                    and bundled_library("/opt/homebrew/lib/libSDL3.0.dylib"))
        root = ROOT / "x.app/Contents/MacOS/x2native"
        print("package_macos selftest: "
              f"complete={complete} no-game-files={no_game} "
              f"launches={launches} library-filter={filtered} "
              f"bundle-root-shape={root.name == 'x2native'}")
        return 0 if (complete and no_game and launches and filtered) else 1
    finally:
        shutil.rmtree(temporary)


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--selftest", action="store_true")
    parser.add_argument("--build-dir", type=Path, default=BUILD / "native")
    parser.add_argument("--output", type=Path,
                        default=BUILD / "release" / f"{BUNDLE_NAME}.app")
    args = parser.parse_args(argv)
    if args.selftest:
        return selftest()
    binary = require_file(args.build_dir / "x2native", "build output")
    build(binary, args.build_dir / "ui", args.output.resolve())
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
