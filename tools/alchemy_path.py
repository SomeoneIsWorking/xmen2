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

from shared_dir import shared_dir


def alchemy_dir():
    """The Alchemy checkout, or a refusal naming every path that was tried."""
    return shared_dir("alchemy", "tools")


def add_alchemy_tools_to_path():
    """Put Alchemy's tools and tracked IGB reader on the import path."""
    root = alchemy_dir()
    tools = os.path.join(root, "tools")
    vendor = os.path.join(root, "vendor")
    for path in (tools, vendor):
        if path not in sys.path:
            sys.path.insert(0, path)
    return tools
