"""Where a SHARED repo lives, resolved in ONE place for the whole port.

Some of what this port uses is not this port's: the Alchemy engine layer is
shared with Marvel Ultimate Alliance, and the RE harness (claims, instruments,
the frontier, the issue catalog) is shared with every port in the tree. Those
live in their own repos under `shared/`, and are consumed rather than vendored
-- see the repo layout README.

Resolution order for `shared/<name>`: `$<NAME>_DIR`, then `$SHARED_DIR/<name>`,
then the sibling checkout the standard layout implies. If none exists this
REFUSES and names every path it tried. It does not fall back to an in-tree
copy, because a stale vendored copy that silently wins is the exact failure
this split exists to end.
"""

import os

_HERE = os.path.dirname(os.path.abspath(__file__))
_PORT_ROOT = os.path.dirname(_HERE)                       # e.g. pc/xmen2
_LAYOUT_SHARED = os.path.normpath(
    os.path.join(_PORT_ROOT, "..", "..", "shared"))


def shared_dir(name, marker=None):
    """The checkout of `shared/<name>`.

    `marker` is a path that must exist inside it, so a directory that happens
    to have the right name but is empty (an uninitialised submodule) is
    reported as missing rather than returned and failing later, further away.
    """
    tried = []
    env = os.environ.get("%s_DIR" % name.replace("-", "_").upper())
    if env:
        tried.append(env)
    if os.environ.get("SHARED_DIR"):
        tried.append(os.path.join(os.environ["SHARED_DIR"], name))
    tried.append(os.path.join(_LAYOUT_SHARED, name))

    for path in tried:
        probe = os.path.join(path, marker) if marker else path
        if os.path.exists(probe):
            return path
    raise SystemExit(
        "shared_dir: no checkout of `shared/%s` found. Looked in:\n" % name
        + "".join("    %s\n" % p for p in tried)
        + ("(each had to contain %r)\n" % marker if marker else "")
        + "It is a separate repo that this port consumes rather than vendors.\n"
          "Clone it into `shared/` beside this port, or set %s_DIR."
          % name.replace("-", "_").upper())
