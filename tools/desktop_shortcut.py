#!/usr/bin/env python3
"""Install the per-machine X-Men Legends II shortcut (never committed).

    desktop_shortcut.py local          menu and Desktop entry here, on the newest native build
    desktop_shortcut.py ally           player install in ~/Games/xmen2/ on `ssh ally`, with its entries
    desktop_shortcut.py launch         what the local entry runs: build/native/x2native, built only if absent

The ally install is the release AppImage (build it with tools/appimage_container.py) and a copy
of the configured PC install; the AppImage finds the game through its own install-path.txt.
"""

from __future__ import annotations

import argparse
import os
from pathlib import Path
import shlex
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parent.parent
APPIMAGE = ROOT / "build" / "release" / "X-Men-Legends-II-x86_64.AppImage"
ICON = ROOT / "packaging" / "xmen2-port.svg"
NATIVE = ROOT / "build" / "native" / "x2native"
ENTRY_ID = "xmen2-port"
CONFIG = Path.home() / ".config" / "xmen2"
HOST = "ally"
REMOTE_HOME = "Games/xmen2"  # relative to the remote home


def refuse(message: str) -> None:
    raise SystemExit(f"desktop_shortcut: {message}")


def entry(exec_line: str) -> str:
    return ("[Desktop Entry]\nType=Application\nName=X-Men Legends II\n"
            "Comment=Native X-Men Legends II port\n"
            f"Exec={exec_line}\nIcon={ENTRY_ID}\nCategories=Game;\nTerminal=false\n"
            "StartupNotify=true\n")


def configured_install() -> Path:
    path_file = CONFIG / "install-path.txt"
    if not path_file.is_file():
        refuse(f"{path_file} is missing; pick the install in the game's setup screen first")
    install = Path(path_file.read_text().strip())
    if not (install / "XMen2.exe").is_file():
        refuse(f"{install} has no XMen2.exe")
    return install


def install_local() -> None:
    uv = shutil.which("uv")
    if uv is None:
        refuse("uv is not on PATH")
    icons = Path.home() / ".local/share/icons/hicolor/scalable/apps"
    icons.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(ICON, icons / f"{ENTRY_ID}.svg")
    text = entry(f'"{uv}" --directory "{ROOT}" run --frozen python tools/desktop_shortcut.py launch')
    for folder in (Path.home() / ".local/share/applications", Path.home() / "Desktop"):
        if not folder.is_dir():
            continue
        path = folder / f"{ENTRY_ID}.desktop"
        path.write_text(text)
        path.chmod(0o755)
        print(f"desktop_shortcut: wrote {path}")


def ssh(command: str) -> None:
    subprocess.run(["ssh", HOST, command], check=True)


def install_ally() -> None:
    if not APPIMAGE.is_file():
        refuse(f"{APPIMAGE} is missing; build it with tools/appimage_container.py")
    install = configured_install()
    if subprocess.run(["ssh", "-o", "ConnectTimeout=5", HOST, "true"]).returncode != 0:
        refuse(f"{HOST} does not answer")
    ssh(f"mkdir -p ~/{REMOTE_HOME}/game ~/.config/xmen2 ~/.local/share/applications "
        "~/.local/share/icons/hicolor/scalable/apps")
    subprocess.run(["rsync", "-a", "--delete", f"{install}/", f"{HOST}:{REMOTE_HOME}/game/"], check=True)
    subprocess.run(["rsync", "-a", str(APPIMAGE), f"{HOST}:{REMOTE_HOME}/{APPIMAGE.name}"], check=True)
    subprocess.run(["rsync", "-a", str(ICON),
                    f"{HOST}:.local/share/icons/hicolor/scalable/apps/{ENTRY_ID}.svg"], check=True)
    # The remote home is filled in on the ally, which owns that path.
    text = entry(f"@HOME@/Games/xmen2/{APPIMAGE.name}")
    script = (
        'set -eu; g="$HOME/Games/xmen2"; '
        'printf "%s\\n" "$g/game" > ~/.config/xmen2/install-path.txt; '
        f'e={shlex.quote(text)}; e=$(printf "%s" "$e" | sed "s#@HOME@#$HOME#"); '
        'for d in ~/.local/share/applications ~/Desktop; do '
        f'[ -d "$d" ] || continue; printf "%s" "$e" > "$d/{ENTRY_ID}.desktop"; '
        f'chmod 755 "$d/{ENTRY_ID}.desktop"; echo "desktop_shortcut: wrote {HOST}:$d/{ENTRY_ID}.desktop"; done')
    ssh(script)


def launch() -> int:
    if not NATIVE.is_file():
        os.execv(sys.executable, [sys.executable, str(ROOT / "tools" / "run.py")])
    os.chdir(ROOT)
    os.execv(str(NATIVE), [str(NATIVE)])
    return 0


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("target", choices=("local", "ally", "launch"))
    target = parser.parse_args(argv).target
    if target == "local":
        install_local()
    elif target == "ally":
        install_ally()
    else:
        return launch()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
