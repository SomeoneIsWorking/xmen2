#!/usr/bin/env python3
"""Run ruff over this repo's Python, and say clearly when it did not run.

Ruff is not a build dependency -- it is a developer tool that may or may not be
installed -- so the interesting case is the one where it is ABSENT. A lint step
that quietly succeeds because the linter was missing is the same shape of
defect as a diagnostic that prints nothing: indistinguishable from a clean
tree. This exits 77 (ctest SKIP) in that case and names what did not happen.

The rule selection, and what is deliberately left out, is in ruff.toml.
"""

import os
import subprocess
import sys

TARGETS = ["tools", "tests"]


def main():
    root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    try:
        version = subprocess.run([sys.executable, "-m", "ruff", "--version"],
                                 capture_output=True, text=True, cwd=root)
    except OSError as e:
        print("lint: could not run python -m ruff (%s)." % e, file=sys.stderr)
        return 77
    if version.returncode != 0:
        print("lint: SKIPPING -- ruff is not installed, so NOTHING was "
              "linted.\n"
              "  This is a skip, not a pass. Install it with:\n"
              "      python3 -m pip install --user ruff", file=sys.stderr)
        return 77

    run = subprocess.run([sys.executable, "-m", "ruff", "check",
                          "--output-format", "concise", *TARGETS],
                         cwd=root, text=True)
    n = sum(len([f for f in files if f.endswith(".py")])
            for _, _, files in
            [(r, d, f) for t in TARGETS
             for r, d, f in os.walk(os.path.join(root, t))])
    print("lint: ruff %s over %d Python file(s) in %s"
          % (version.stdout.strip().split()[-1], n, "/".join(TARGETS)))
    return 1 if run.returncode else 0


if __name__ == "__main__":
    sys.exit(main())
