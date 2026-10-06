"""Find an LLVM command-line tool as installed, including kegs not on PATH."""

from __future__ import annotations

from pathlib import Path
import shutil

# Homebrew does not link its llvm formula into /opt/homebrew/bin, because doing
# so would shadow Apple's toolchain. Only looking at PATH therefore reports the
# tool missing on a machine that has it, and a check built on it silently stops
# running on every such developer's machine.
HOMEBREW_KEGS = ("/opt/homebrew/opt/llvm/bin", "/usr/local/opt/llvm/bin")


def find_llvm_tool(name: str) -> str | None:
    found = shutil.which(name)
    if found:
        return found
    for keg in HOMEBREW_KEGS:
        candidate = Path(keg) / name
        if candidate.is_file():
            return str(candidate)
    return None
