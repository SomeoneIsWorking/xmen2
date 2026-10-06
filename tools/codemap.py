#!/usr/bin/env python3
"""codemap — shared subsystem-ownership mapper, run from this port.

The tool itself lives in the `re-harness` repo, because every port in the
tree needs it. The DATA it reads and checks (docs/codemap.md and this port's
source tree) stays here; the tool resolves it from the working directory,
which is why this shim only has to find the tool and hand over.
"""
import os
import runpy
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from shared_dir import shared_dir

_tool = os.path.join(
    shared_dir("re-harness", "tools/codemap.py"),
    "tools",
    "codemap.py",
)

if __name__ == "__main__":
    sys.argv[0] = _tool
    runpy.run_path(_tool, run_name="__main__")
else:
    globals().update(runpy.run_path(_tool))
