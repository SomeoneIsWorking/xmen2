"""Where the Alchemy engine layer lives, resolved in ONE place.

The IGB readers, the XMLB container and the ARK tooling are not this port's --
they belong to the Alchemy engine, which X-Men Legends II shares with Marvel
Ultimate Alliance on a different CPU entirely. They live in their own repo
(`shared/alchemy`), so a port consumes them rather than owning a copy.

Resolution order: `$ALCHEMY_DIR`, then the sibling checkout implied by the
standard repo layout (`shared/` beside `pc/`, see the layout README). If
neither exists this REFUSES -- importing a stale in-tree copy that no longer
exists, or silently continuing without the tool, would both look like a
working setup right up until the output was wrong.
"""

import os
import sys

_HERE = os.path.dirname(os.path.abspath(__file__))
_PORT_ROOT = os.path.dirname(_HERE)                       # pc/xmen2
_LAYOUT_DEFAULT = os.path.normpath(
    os.path.join(_PORT_ROOT, "..", "..", "shared", "alchemy"))


def alchemy_dir():
    """The Alchemy checkout, or a refusal naming both places that were tried."""
    candidates = []
    env = os.environ.get("ALCHEMY_DIR")
    if env:
        candidates.append(env)
    candidates.append(_LAYOUT_DEFAULT)
    for path in candidates:
        if os.path.isdir(os.path.join(path, "tools")):
            return path
    raise SystemExit(
        "alchemy_path: no Alchemy checkout found. Looked in:\n"
        + "".join("    %s\n" % c for c in candidates)
        + "The IGB/XMLB/ARK tooling lives in the `alchemy` repo, which this\n"
          "port consumes rather than vendors. Clone it beside this one as\n"
          "`shared/alchemy`, or set ALCHEMY_DIR.")


def add_alchemy_tools_to_path():
    """Put Alchemy's tools on sys.path so `import xmlb` resolves."""
    tools = os.path.join(alchemy_dir(), "tools")
    if tools not in sys.path:
        sys.path.insert(0, tools)
    return tools
