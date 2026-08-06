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

    Past the last table the "entries" are instruction bytes -- 0x0066d645's ten
    real entries are followed by 0x24748b56, which is `AND [ESP+...]`-shaped,
    not an address. An entry stays an entry while it points into the SAME
    executable block as the jump itself.

    ITS BLIND SPOT, which cost a session: this cannot see the boundary between
    two ADJACENT tables. 0x005fb240 holds 4 entries and 0x005fb250 holds 7, and
    all eleven dwords are valid in-block code addresses, so this reads straight
    through the first table into the second. Use table_bound() below; fall back
    to this only when the switch has no visible range check, and SAY which was
    used.
    """
    return bool(is_executable and in_same_block)


# `CMP <reg>,<imm>` -- MSVC's switch range check.
CMP_IMM = re.compile(r"^CMP\s+([A-Z]{2,3})\s*,\s*(0x[0-9a-fA-F]+|\d+)\s*$")
# The mnemonic and its first operand, for "does this write the index register".
DEST = re.compile(r"^([A-Z]+)\s+([A-Za-z0-9_]+)\s*(?:,|$)")
# Instructions that do not write their first operand.
NON_WRITING = ("CMP", "TEST", "PUSH", "JMP", "CALL", "RET", "NOP")


def table_bound(index_reg, prior):
    """How many entries the table REALLY has, from the switch's own range check.

    `prior` is the instruction text of what precedes the `JMP [reg*4 + table]`,
    in program order, nearest last. MSVC emits

        CMP  <index>, N
        JA   <default>
        JMP  dword ptr [<index>*4 + <table>]

    so the count is N + 1 -- read out of the code, not guessed from where the
    data stops looking like addresses.

    Returns (count, why) with count None when the bound cannot be established.
    None is a real answer here: the caller falls back to plausible_entry() and
    reports that it did. Inventing a number would be worse than the heuristic,
    because it would look like the table had been read.
    """
    if not prior:
        return None, ("no range check before the jump (nothing precedes it in "
                      "this function) -- the bound is not visible here")
    guard = prior[-1].strip()
    mnem = guard.split()[0].upper() if guard.split() else ""
    if mnem != "JA":
        return None, ("no range check before the jump: it is preceded by %s, "
                      "not the JA that guards an MSVC switch" % (mnem or "?"))

    for text in reversed(prior[:-1]):
        m = CMP_IMM.match(text.strip())
        if m:
            reg, imm = m.group(1).upper(), m.group(2)
            if reg != index_reg.upper():
                return None, ("the range check tests %s but the jump indexes "
                              "with %s -- not this switch's bound"
                              % (reg, index_reg.upper()))
            return int(imm, 0) + 1, "bounded by `%s` + JA" % text.strip()
        d = DEST.match(text.strip())
        if d and d.group(1).upper() not in NON_WRITING \
                and d.group(2).upper() == index_reg.upper():
            return None, ("`%s` writes %s between the range check and the "
                          "jump, so no earlier CMP describes the value the "
                          "jump indexes with" % (text.strip(), index_reg))
    return None, ("a JA guards the jump but no `CMP %s,<imm>` precedes it -- "
                  "the bound is not visible here" % index_reg.upper())


# Instructions after which control does not fall through, so a body ending in
# one is COMPLETE. RET/JMP are the two this image uses; the rest are here so a
# body ending in them is not mistaken for a truncated one.
TERMINATORS = ("RET", "RETF", "RETN", "JMP", "IRET", "IRETD", "UD2", "HLT")


def is_terminator(mnemonic):
    """Does a body ending in this instruction end for a REASON?

    The point of the question: a merge repairs a body that was CUT OFF, and a
    body ending in a terminator was not cut off. MergeTruncated.py used to
    assert "ends without a terminator" in its message without ever checking it,
    and on that basis absorbed a 3-byte padding function into a body that ends
    in RET -- reporting "1 repaired" for a body that did not change.
    """
    return mnemonic.strip().upper() in TERMINATORS


def merge_outcome(inner_name, before, after):
    """Did absorbing `inner_name` actually repair anything? (ok, message)

    "Ran successfully and changed nothing" is the outcome that reads as a fix
    and is not one, so the count of instructions in the body before and after
    is the verdict -- not the fact that the absorb completed.
    """
    if after > before:
        return True, ("re-created with %d instructions, up from %d"
                      % (after, before))
    if after < before:
        return False, ("the body SHRANK from %d to %d instructions after "
                       "absorbing %s -- this made things worse, and the "
                       "instructions it lost now belong to nothing"
                       % (before, after, inner_name))
    return False, ("absorbing %s did not grow the body (%d instructions "
                   "before and after) -- NOTHING was repaired, whatever was "
                   "absorbed was not part of this function"
                   % (inner_name, before))


def check_absorbed(unmade, body):
    """Did the re-created body actually take in every label that was un-made?

    Un-making without absorbing is worse than doing nothing: the label stops
    being a function AND is in no function, so the runtime dispatches to it and
    finds no body at all. That is what happened to 0x005fafc1, which was read
    out of the WRONG table and could never have been part of the container
    being repaired.

    Returns (ok, message).
    """
    if not unmade:
        return True, "no case label was un-made, so none had to be absorbed"
    body = set(body)
    orphans = [a for a in unmade if a not in body]
    if orphans:
        return False, ("ORPHANED %d of %d un-made case label(s) -- now in NO "
                       "function, which the runtime will hit as a missing "
                       "body: %s"
                       % (len(orphans), len(unmade),
                          ", ".join("0x%08x" % a for a in orphans)))
    return True, ("all %d un-made case label(s) were absorbed into the body"
                  % len(unmade))
