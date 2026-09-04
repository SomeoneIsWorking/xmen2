#!/usr/bin/env python3
"""Run x86port's shared PE32 inspector from this port.

PE image inspection is runtime provisioning and native-DLL interop support,
not guest-code generation. x86port owns the reusable parser; this title keeps
only the resolver shim.
"""
import os
import runpy
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from shared_dir import shared_dir

_tool = os.path.join(shared_dir("x86port", "tools/pe.py"), "tools", "pe.py")

if __name__ == "__main__":
    sys.argv[0] = _tool
    runpy.run_path(_tool, run_name="__main__")
else:
    globals().update(runpy.run_path(_tool))
