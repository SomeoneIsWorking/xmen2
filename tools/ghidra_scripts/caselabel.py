# Which switch case labels may be un-made, and where a jump table ends.
#
# Imported by RecreateFunction.py inside Ghidra (Jython puts the script's own
# directory on sys.path -- the same arrangement x2out.py documents), and by
# tests/test_caselabel.py in CPython. Nothing here imports `ghidra`, on
# purpose: the rule that decides when to DELETE a function object is the part
# that must not be wrong, and it must be testable without a 40-minute re-lift.
#
# THE PROBLEM. A case label seeded as a function stops the container's flow
# walk dead: Ghidra will not absorb another function's entry point, so the case
# block and everything falling through from it stay outside the container for
# good. XMen2.exe 0x0066cf4e wired all 10 entries of its table and still ended
# with a 127-byte hole because entry 0 (0x0066cf79) was FUN_0066cf79 -- a
# single `CMP dword ptr [EBP + 0x8],0x3` with no terminator (issue #27).
#
# WHY THIS IS NOT A FORCE FLAG. The address is not guessed to be a case label:
# it was read out of the container's own jump table, four bytes at a time.
# What `classify` weighs is only whether something ELSE also treats it as an
# entry point -- a CALL reference, or a name a human attached. Either of those
# means the container really is non-contiguous there and deleting the function
# would break its callers, so both refuse.
import re

DEFAULT_NAME = re.compile(r"^(FUN|SUB)_[0-9a-fA-F]+$")


def classify(container, container_name, table, target, inner_name, call_refs):
    """Decide what to do with one jump-table target. Returns (action, message).

    `inner_name` is the name of the function starting exactly at `target`, or
    None if no function starts there. `call_refs` is how many references to
    `target` are calls.

    Actions: "absorb" (delete the function object so the container can take
    the case), "keep-called", "keep-named", "skip-self", "not-a-function".
    Every action carries the message to print, because a decision this tool
    makes silently is one nobody can audit afterwards.
    """
    if inner_name is None:
        return ("not-a-function",
                "case label 0x%08x (table 0x%08x) is not a function entry -- "
                "the flow walk reaches it by itself" % (target, table))
    if target == container:
        return ("skip-self",
                "case label 0x%08x (table 0x%08x) IS %s's own entry -- a "
                "switch that jumps to the top of its function, left alone"
                % (target, table, container_name))
    if call_refs:
        return ("keep-called",
                "case label 0x%08x (table 0x%08x) is %s with %d CALL "
                "reference(s) -- a real function, kept. %s stays "
                "non-contiguous there."
                % (target, table, inner_name, call_refs, container_name))
    if not DEFAULT_NAME.match(inner_name):
        return ("keep-named",
                "case label 0x%08x (table 0x%08x) is named %s, not a Ghidra "
                "default -- refusing to delete it, kept."
                % (target, table, inner_name))
    return ("absorb",
            "case label 0x%08x (table 0x%08x) was %s with no CALL references "
            "-- un-making it so %s can absorb the case"
            % (target, table, inner_name, container_name))


def summarize(table, cases, unmade, kept):
    """One line per table, worded so a negative cannot be mistaken for a look
    that never happened."""
    if not cases:
        return ("table 0x%08x wired no entries, so NO case label could be "
                "examined" % table)
    if not unmade and not kept:
        return ("none of the %d case label(s) of table 0x%08x is a function; "
                "nothing to un-make" % (cases, table))
    return ("table 0x%08x: %d of %d case label(s) un-made, %d kept as real "
            "function(s)" % (table, unmade, cases, kept))


def plausible_entry(target, in_same_block, is_executable):
    """Is this dword still a jump-table entry, or the code after the table?

    The tables in this image are adjacent (0x005fb240 and 0x005fb250), so a
    fixed entry count runs one table into the next; and past the last table the
    "entries" are instruction bytes -- 0x0066d645's ten real entries are
    followed by 0x24748b56, which is `AND [ESP+...]`-shaped, not an address.
    An entry stays an entry while it points into the SAME executable block as
    the jump itself.
    """
    return bool(is_executable and in_same_block)
