#!/usr/bin/env python3
"""project_state — shared project-state validator, run from this port.

The tool itself lives in the `re-harness` repo, because every port in the
tree needs it. The DATA it validates (docs/project-state.md, project goals,
and issue links) is this port's and stays here; the harness resolves it from
the working directory, which is why this shim only has to find the tool and
hand over.
"""
import os
import runpy
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from shared_dir import shared_dir

_tool = os.path.join(
    shared_dir("re-harness", "tools/project_state.py"),
    "tools",
    "project_state.py",
)

if __name__ == "__main__":
    sys.argv[0] = _tool
    runpy.run_path(_tool, run_name="__main__")
else:
    globals().update(runpy.run_path(_tool))
