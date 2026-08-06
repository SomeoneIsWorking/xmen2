#!/usr/bin/env python3
"""Which function contains each address, and does that function look real?

    tools/whose_function.py <module>.json <file-of-hex-addresses>
    tools/whose_function.py <module>.json 0x005fad31 0x005fb2bc

Written for one job: before a split CARVES a function that analysis already
found, show what is about to be cut. Issue #21 is what happens without it --
the discovery loop carved one SEH-protected function into five pieces across
three sessions, and every piece's RET then popped part of the exception frame
instead of a return address. Nothing in the loop's output said a function was
being destroyed.

## The signal to look for

An MSVC function that installs an exception frame begins

    PUSH -0x1
    PUSH <handler address>
    MOV EAX,FS:[0x0]

`FS:[0]` is the thread's exception-registration chain, and the two pushes are
the record being linked into it. A seed landing INSIDE such a function is
almost always a handler or scope-table pointer that only looks like a call
target, so splitting there is destructive. This flags it rather than deciding:
the call is the operator's.

An address that is its own function's entry, or that no function contains, is
reported as such -- both are fine and neither is a carve.
"""
import json
import re
import sys

SEH = re.compile(r"^MOV\s+E[A-Z]{2},FS:\[0x0\]$")


def looks_seh(fn):
    """True if the entry installs an exception frame."""
    for ins in fn.get("ins", ())[:6]:
        if SEH.match(ins.get("t", "").strip()):
            return True
    return False


def main(argv):
    if len(argv) < 2:
        sys.exit(__doc__)
    with open(argv[0]) as f:
        d = json.load(f)

    addrs = []
    for a in argv[1:]:
        try:
            with open(a) as f:
                addrs += [int(x, 16) for x in f.read().split() if x.strip()]
        except IOError:
            addrs.append(int(a, 16))
    if not addrs:
        # Refuse rather than print a clean bill of health for an empty list.
        sys.exit("whose_function: no addresses given -- checked NOTHING")

    # ep -> function, plus the set of instruction addresses each covers.
    fns = d["functions"]
    owner = {}
    for fn in fns:
        for ins in fn.get("ins", ()):
            owner[ins["a"]] = fn

    flagged = 0
    for a in sorted(set(addrs)):
        fn = owner.get(a)
        if fn is None:
            print("      0x%08x  in NO detected function -- a seed here "
                  "creates one, it does not carve" % a)
            continue
        if fn["ep"] == a:
            print("      0x%08x  IS the entry of %s -- nothing to carve"
                  % (a, fn["qname"]))
            continue
        first = " | ".join(i["t"] for i in fn.get("ins", ())[:3])
        seh = looks_seh(fn)
        if seh:
            flagged += 1
        print("      0x%08x  inside %s (entry 0x%08x, %d ins)%s\n"
              "                  first: %s"
              % (a, fn["qname"], fn["ep"], len(fn.get("ins", ())),
                 "   <<< SEH PROLOGUE -- DO NOT SPLIT" if seh else "",
                 first))

    print("      %d of %d address(es) fall inside a function with an SEH "
          "prologue." % (flagged, len(set(addrs))))
    if flagged:
        print("      Splitting those destroys a real function; see issue #21.")
    return 1 if flagged else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
