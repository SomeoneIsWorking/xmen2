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

## What it shows, and what it does NOT decide

Two things, side by side, because neither alone settles it:

  * the CONTAINING function's first instructions. `PUSH -0x1; PUSH <handler>;
    MOV EAX,FS:[0x0]` is an MSVC exception frame being linked into the chain
    at `FS:[0]`, and carving such a function is how issue #21 happened.
  * the CANDIDATE's own first instructions. An address that begins mid-flow
    (`LEA EDX,[ESP + 0x18]` reading a slot it never established) is not a
    function. One that begins `PUSH EBP; PUSH <imm>` plausibly is.

An earlier version printed only the first and called it "DO NOT SPLIT". That
was too strong and it was wrong in this very region: after FUN_005fac10 was
merged back together, 0x005facd5 -- which begins `PUSH EBP; PUSH 0x6a3d08;
PUSH 0x4` and is dispatched to indirectly at run time -- sits inside it and
would have been refused. The region holds a MIX of real indirectly-called
entries and bad boundaries, so the SEH prologue is EVIDENCE, not a verdict.

The call is the operator's. This prints what the call needs.

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


# Instruction forms a real function ENTRY starts with. Not exhaustive, and it
# does not need to be: it is used to REFUSE a split, so a false "looks like an
# entry" costs nothing and a false "does not" costs a refusal that a human
# reads.
ENTRY_FORMS = (
    "PUSH EBP", "MOV EBP,ESP", "SUB ESP", "PUSH EBX", "PUSH ESI", "PUSH EDI",
    "MOV EAX,dword ptr [ESP", "MOV ECX,dword ptr [ESP", "MOV EDX,dword ptr [ESP",
    "PUSH 0x", "PUSH dword ptr [ESP", "LEA EAX,[ESP", "XOR EAX,EAX",
    "MOV EAX,", "JMP ", "RET", "TEST ", "CMP ",
)


def looks_like_entry(text):
    """Would a compiler have started a function with this instruction?

    A split at an address that fails this carves a real function in half. That
    is the issue #21 dead end, and the SEH check alone does not catch it:
    XMen2.exe 0x005fafc1 begins `LEA EDI,[ESI + 0x28]` -- ESI holding a live
    object from the CALLER -- which no function entry does, and splitting there
    produced a body whose RET popped something that was not a return address.
    """
    t = text.strip()
    return any(t.startswith(f) for f in ENTRY_FORMS)


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
        # The candidate's OWN first instructions: this is what distinguishes a
        # real indirectly-called entry from a mid-flow address, and the earlier
        # version never showed it.
        ins = fn.get("ins", ())
        at = [i["t"] for i in ins if i["a"] >= a][:3]
        seh = looks_seh(fn)
        # The candidate's own first instruction decides whether a split makes a
        # FUNCTION or carves one. See looks_like_entry.
        entryish = looks_like_entry(at[0]) if at else False
        if seh or not entryish:
            flagged += 1
        why = ("   <<< container has an SEH prologue" if seh else
               "   <<< does NOT look like a function entry" if not entryish
               else "")
        print("      0x%08x  inside %s (entry 0x%08x, %d ins)%s\n"
              "                  container starts: %s\n"
              "                  candidate starts: %s"
              % (a, fn["qname"], fn["ep"], len(ins), why,
                 first, " | ".join(at) or "(no instructions at or after it)"))

    print("      %d of %d address(es) must NOT be split -- an SEH prologue in "
          "the container, or a candidate\n      whose first instruction is "
          "not one a function begins with." % (flagged, len(set(addrs))))
    if flagged:
        print("      That is a REASON TO LOOK, not a verdict. Carving such a "
              "function is how issue #21\n"
              "      happened -- and the same region also holds real "
              "indirectly-called entries that\n"
              "      were wrongly absorbed. Compare the two 'starts:' lines: a "
              "candidate beginning\n"
              "      mid-flow is not a function; one beginning with a prologue "
              "probably is.")
    return 1 if flagged else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
