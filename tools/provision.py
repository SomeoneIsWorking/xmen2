#!/usr/bin/env python3
"""Provision the native generated inputs without building or launching the game."""

from __future__ import annotations

from pathlib import Path
import sys


ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT))

import bootstrap  # noqa: E402


def main() -> int:
    if len(sys.argv) != 1:
        raise SystemExit("provision: takes no arguments")
    bootstrap.initialize()
    print("bootstrap: provisioning complete")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
