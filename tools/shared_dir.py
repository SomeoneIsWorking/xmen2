"""Where a SHARED repo lives, resolved in ONE place for the whole port.

Some of what this port uses is not this port's: the Alchemy engine layer is
shared with Marvel Ultimate Alliance, and the RE harness (claims, instruments,
the frontier, the issue catalog) is shared with every port in the tree. Those
live in their own repos under `shared/`, and are consumed rather than vendored
-- see the repo layout README.

Resolution order for `shared/<name>`: `$<NAME>_DIR`, then `$SHARED_DIR/<name>`,
then the project-local provisioned checkout under `vendor/shared/`. If none
exists this REFUSES and names every path it tried.

There is deliberately NO fallback to a sibling clone outside the port: a
consumer must resolve one explicit checkout and refuse when it is absent.
"""

import os

_HERE = os.path.dirname(os.path.abspath(__file__))
_PORT_ROOT = os.path.dirname(_HERE)                       # e.g. pc/xmen2

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
    tried.append(os.path.join(_PORT_ROOT, "vendor", "shared", name))

    for path in tried:
        probe = os.path.join(path, marker) if marker else path
        if os.path.exists(probe):
            return path
    raise SystemExit(
        "shared_dir: no checkout of `shared/%s` found. Looked in:\n" % name
        + "".join("    %s\n" % p for p in tried)
        + ("(each had to contain %r)\n" % marker if marker else "")
        + "It is a separate repo, provisioned into vendor/shared by "
          "./run.sh.\n"
          "Run ./run.sh, or set %s_DIR to a checkout."
          % name.replace("-", "_").upper())


def optional_maintainer_dir(name, marker, check_name):
    """Resolve a maintainer-only tool, or return CTest's standard skip code."""
    try:
        return shared_dir(name, marker)
    except SystemExit:
        print(
            f"{check_name}: SKIP — maintainer-only {name} is unavailable; "
            f"set {name.replace('-', '_').upper()}_DIR to run this check."
        )
        raise SystemExit(77) from None
