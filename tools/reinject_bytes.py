#!/usr/bin/env python3
"""Run the pinned shared recompiler's encoding reinjector for this port."""

import os
import runpy
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from shared_dir import shared_dir

_tool = os.path.join(
    shared_dir("recomp-x86", "tools/reinject_bytes.py"),
    "tools",
    "reinject_bytes.py",
)

if __name__ == "__main__":
    sys.argv[0] = _tool
    runpy.run_path(_tool, run_name="__main__")
else:
    globals().update(runpy.run_path(_tool))
