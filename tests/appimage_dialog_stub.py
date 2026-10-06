#!/usr/bin/env python3
"""Deterministic Zenity boundary for the AppImage setup release probe."""

from __future__ import annotations

import json
import os
from pathlib import Path
import sys


def main() -> int:
    log_value = os.environ.get("X2_DIALOG_LOG")
    selection = os.environ.get("X2_DIALOG_SELECTION")
    if not log_value or not selection:
        print("appimage dialog stub: probe environment is incomplete", file=sys.stderr)
        return 2
    log = Path(log_value)
    with log.open("a", encoding="utf-8") as output:
        output.write(json.dumps(sys.argv[1:]) + "\n")
    if "--version" in sys.argv:
        print("4.0.0")
        return 0
    if "--question" in sys.argv:
        print("Browse")
        return 0
    if "--file-selection" in sys.argv:
        print(selection)
        return 0
    print("appimage dialog stub: unsupported Zenity operation", file=sys.stderr)
    return 2


if __name__ == "__main__":
    raise SystemExit(main())
