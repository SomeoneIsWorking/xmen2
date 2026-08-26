#!/usr/bin/env python3
"""recomp_overrides — shared x86 translator toolchain, run from this port.

The tool lives in the `recomp-x86` repo: the x86-32 lifter serves this PC port
and an original-Xbox one alike (OG Xbox is x86), so it is not this port's to
own. It resolves the PORT from the working directory, which is why this shim
only has to find it and hand over.

`scan_overrides` moved the OTHER way: recomp-x86 5a241a6 took native overrides
out of the emitter entirely and dropped its scanner with them, but this port's
wiring audits (check_save_trace_wiring, check_continue_wiring,
check_autosave_wiring) still cross-check their seams against the
`x86_register_override("<module>", 0x.., fn)` declarations in src/native. The
declarations are a port-side convention, so the scanner lives here now,
carried over verbatim from the shared revision that dropped it.
"""
import os
import re
import runpy
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from shared_dir import shared_dir

_tool = os.path.join(shared_dir("recomp-x86", "tools/recomp_overrides.py"), "tools", "recomp_overrides.py")

if __name__ == "__main__":
    sys.argv[0] = _tool
    runpy.run_path(_tool, run_name="__main__")
else:
    globals().update(runpy.run_path(_tool))


def scan_overrides(root):
    """The override registrations, read from the register_override calls in the
    hand-written host C. Returns a list of (module, linked_ep, file, line).

    Each registration names the MODULE that owns the entry point, because a
    bare address is not a key here: every libIG*.dll is linked for 0x10000000,
    so one linked address is a real function in eight of this game's modules.
    Matching on the address alone routed calls through the dispatcher in every
    one of them and let an override intended for a relocated module fire for
    whichever module kept the preferred base."""
    native = os.path.join(root, "src", "native")
    if not os.path.isdir(native):
        raise SystemExit(
            "recomp: %s does not exist -- cannot scan for "
            "x86_register_override calls. Refusing rather than emitting a "
            "module whose native overrides would never fire.\n"
            "  The translator is SHARED and resolves the port from the working "
            "directory, so run it from the port's root, not from the "
            "translator's." % native)
    pat = re.compile(r'x86_register_override\(\s*"([^"]+)"\s*,\s*'
                     r'0x([0-9a-fA-F]+)')
    bare = re.compile(r'x86_register_override\(\s*0x')
    out = []
    for fn in sorted(os.listdir(native)):
        if not fn.endswith(".c"):
            continue
        path = os.path.join(native, fn)
        with open(path) as f:
            text = f.read()
        if bare.search(text):
            raise SystemExit(
                "recomp: %s calls x86_register_override with a bare address "
                "and no module name. One linked address names a different "
                "function in every module linked for the same base, so the "
                "emitter cannot tell which one to route." % path)
        for m in pat.finditer(text):
            line = text.count("\n", 0, m.start()) + 1
            out.append((m.group(1), int(m.group(2), 16), path, line))
    return out
