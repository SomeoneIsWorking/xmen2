#!/usr/bin/env python3
"""re_frontier — shared RE harness, run from this port.

The tool itself lives in the `re-harness` repo, because every port in the
tree needs it and nine forked copies is how it drifted into nine different
versions. The roadmap it reads is the decomp's (external/xmen2-decomp), named
through RE_FRONTIER_ROADMAP; the rest of its data is resolved from the working
directory.
"""
import os
import runpy
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from shared_dir import shared_dir

_tool = os.path.join(shared_dir("re-harness", "tools/re_frontier.py"), "tools", "re_frontier.py")
os.environ.setdefault("RE_FRONTIER_ROADMAP", os.path.join(
    os.path.dirname(os.path.dirname(os.path.abspath(__file__))),
    "external", "xmen2-decomp", "docs", "re-frontier.md"))

if __name__ == "__main__":
    sys.argv[0] = _tool
    runpy.run_path(_tool, run_name="__main__")
else:
    # Imported, not run: hand back the real module's namespace. Running the CLI
    # on import would make `import re_frontier` print usage and exit, which is
    # how this shim first broke a test that imports the tool to exercise its
    # writer directly.
    globals().update(runpy.run_path(_tool))
