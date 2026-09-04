#!/usr/bin/env python3
"""xmlb — Alchemy engine tooling, run from this port.

The tool lives in the `alchemy` repo: the IGB/ARK layer belongs to the engine
X-Men Legends II shares with Marvel Ultimate Alliance, so it is not this port's
to own. This shim only has to find it and hand over; the DATA it reads stays
here (`scratch/analysis/*.ark.json`).
"""

import os
import runpy
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from shared_dir import shared_dir

_tool = os.path.join(shared_dir("alchemy", "tools/xmlb.py"), "tools", "xmlb.py")

if __name__ == "__main__":
    sys.argv[0] = _tool
    runpy.run_path(_tool, run_name="__main__")
else:
    globals().update(runpy.run_path(_tool))
