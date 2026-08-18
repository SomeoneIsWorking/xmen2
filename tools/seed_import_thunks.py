#!/usr/bin/env python3
"""seed_import_thunks — shared x86 translator toolchain, run from this port.

The tool lives in the `recomp-x86` repo: the x86-32 lifter serves this PC port
and an original-Xbox one alike (OG Xbox is x86), so it is not this port's to
own. It resolves the PORT from the working directory, which is why this shim
only has to find it and hand over.
"""
import os
import runpy
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from shared_dir import shared_dir                            # noqa: E402

_tool = os.path.join(shared_dir("recomp-x86", "tools/seed_import_thunks.py"), "tools", "seed_import_thunks.py")

if __name__ == "__main__":
    sys.argv[0] = _tool
    runpy.run_path(_tool, run_name="__main__")
else:
    globals().update(runpy.run_path(_tool))
