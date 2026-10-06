#!/usr/bin/env python3
"""info — shared RE harness, run from this port.

The tool itself lives in the `re-harness` repo, because every port in the
tree needs it and nine forked copies is how it drifted into nine different
versions. The DATA it reads (docs/info, docs/issues, docs/re-frontier.md) is
this port's and stays here; the harness resolves it from the working
directory, which is why this shim only has to find the tool and hand over.
"""
import os
import runpy
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from shared_dir import shared_dir

_tool = os.path.join(shared_dir("re-harness", "info.py"), "info.py")

if __name__ == "__main__":
    sys.argv[0] = _tool
    runpy.run_path(_tool, run_name="__main__")
else:
    # Imported, not run: hand back the real module's namespace. Running the CLI
    # on import would make `import re_frontier` print usage and exit, which is
    # how this shim first broke a test that imports the tool to exercise its
    # writer directly.
    globals().update(runpy.run_path(_tool))
