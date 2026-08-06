#!/usr/bin/env python3
"""Unit tests for the translator, tools/recomp.py.

Run: python3 tests/test_recomp.py     (also wired in as the `recomp` ctest)

## Why this file exists

The translator had no tests at all, and it is the most defect-prone thing in
the project: a mistranslation produces a binary that runs and is wrong. The
only oracle was a full re-lift and a game run -- roughly forty minutes, and it
reports "stopped at 0x005facd5" rather than which translation rule is wrong.

`translate(fn)` takes a function dict and returns C lines, so a case is a
handful of instructions and runs in under a second. Each test below encodes a
BEHAVIOUR as emitted C, not a count of anything.

Instruction dicts here match the exporter's shape: `a` address, `m` mnemonic,
`n` length, `t` Ghidra's text, optional `flow` for a resolved branch target and
optional `ind` for an indirect one.
"""
import os
import sys
import unittest

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "tools"))
import recomp                                              # noqa: E402


def ins(a, m, t, n=2, **kw):
    d = {"a": a, "m": m, "t": t, "n": n, "b": "90"}
    d.update(kw)
    return d


def translate(instructions, ep=None):
    """Translate a synthetic function and return its C body as one string."""
    fn = {"ep": ep if ep is not None else instructions[0]["a"],
          "qname": "Test::fn", "name": "fn", "thunk": False,
          "size": sum(i["n"] for i in instructions), "ins": instructions}
    recomp.IMG[0], recomp.IMG[1] = 0x00400000, 0x00a75000
    recomp.KNOWN_EPS.clear()
    recomp.KNOWN_EPS.add(fn["ep"])
    body, reason = recomp.translate(fn)
    if body is None:
        raise AssertionError("translate refused: %s" % reason)
    return "\n".join(body)


class SwitchDispatch(unittest.TestCase):
    """The MSVC switch: `JMP dword ptr [reg*4 + <table>]`.

    Its case labels are addresses INSIDE the same function, so the jump has to
    resolve locally. Sending it to the global dispatcher reports a case label
    as a missing function -- which is what happened, and what then drove the
    discovery loop to seed case labels and carve the function apart across
    three sessions (issue #21, C123).

    Ghidra marks this form with NEITHER `ind` NOR `flow`, which is the whole
    trap: a check for `ind` alone silently skips every switch in the image.
    """

    SWITCH = "JMP dword ptr [EAX*0x4 + 0x5fb240]"

    def test_switch_jmp_resolves_locally(self):
        c = translate([
            ins(0x005fac10, "PUSH", "PUSH EBP", n=1),
            ins(0x005facce, "JMP", self.SWITCH, n=7),
            ins(0x005facd5, "MOV", "MOV EAX,0x1", n=5),   # a case label
            ins(0x005facda, "RET", "RET", n=1),
        ])
        self.assertIn("goto L_injmp;", c,
                      "the switch must jump to the function's own dispatcher")
        self.assertIn("L_injmp:", c, "that dispatcher must be emitted")
        self.assertIn("goto L_005facd5;", c,
                      "the case label must be reachable from it")
        self.assertIn("L_005facd5:", c,
                      "and must have a label of its own")

    def test_switch_jmp_does_not_reach_the_global_dispatcher(self):
        """The regression itself, stated as the thing that must not appear.

        Before the fix this emitted `DISPATCH(C, ...); return;` at the jump,
        and the runtime then reported the case label as
        "no recompiled body at 0x005facd5".
        """
        c = translate([
            ins(0x005fac10, "PUSH", "PUSH EBP", n=1),
            ins(0x005facce, "JMP", self.SWITCH, n=7),
            ins(0x005facd5, "RET", "RET", n=1),
        ])
        jump_line = [l for l in c.splitlines() if "_injmp =" in l or
                     ("DISPATCH" in l and "L_injmp" not in l)]
        self.assertTrue(jump_line, "the switch emitted nothing recognisable")
        self.assertNotIn("DISPATCH(C, RD32", "\n".join(jump_line),
                         "the switch must not dispatch globally")

    def test_every_instruction_is_labelled_when_a_switch_is_present(self):
        """A case label is not a branch target of any direct jump, so nothing
        else would give it a label."""
        c = translate([
            ins(0x00401000, "PUSH", "PUSH EBP", n=1),
            ins(0x00401001, "NOP", "NOP", n=1),
            ins(0x00401002, "JMP", self.SWITCH, n=7),
            ins(0x00401009, "RET", "RET", n=1),
        ])
        for a in (0x00401000, 0x00401001, 0x00401002, 0x00401009):
            self.assertIn("L_%08x:" % a, c, "0x%08x has no label" % a)


class OrdinaryJumps(unittest.TestCase):
    """The fix widened which JMPs count as indirect. These pin down that it
    did not change anything else."""

    def test_direct_jump_is_still_a_plain_goto(self):
        c = translate([
            ins(0x00401000, "JMP", "JMP 0x00401004", n=2, flow=0x00401004),
            ins(0x00401002, "NOP", "NOP", n=2),
            ins(0x00401004, "RET", "RET", n=1),
        ])
        self.assertIn("goto L_00401004;", c)
        self.assertNotIn("L_injmp", c,
                         "a function with only direct jumps needs no local "
                         "dispatcher, and paying for one would label every "
                         "instruction in the image")

    def test_jump_through_a_register_still_reaches_the_dispatcher(self):
        """A tail call through a register leaves the function, so the local
        dispatcher must fall through to the global one rather than swallow
        it. The default case is what does that."""
        c = translate([
            ins(0x00401000, "JMP", "JMP EAX", n=2),
            ins(0x00401002, "RET", "RET", n=1),
        ])
        self.assertIn("goto L_injmp;", c)
        self.assertIn("DISPATCH(C, _injmp)", c,
                      "an out-of-function target must still dispatch")


class SetjmpIsEmittedInline(unittest.TestCase):
    """setjmp cannot be an import stub.

    A host longjmp resumes into a frame that must still be alive, and an
    import stub's frame is dead the moment it returns -- so the host setjmp
    has to be emitted in the body that contains the guest's call site. These
    check both halves of that, and the SECOND is the one that matters: the
    first version of this feature hooked the import call site, MSVC routes
    the call through a thunk instead, and it emitted nothing at all while
    reporting success."""

    def setUp(self):
        recomp.IAT.clear()
        recomp.SETJMP_THUNKS.clear()

    def tearDown(self):
        recomp.IAT.clear()
        recomp.SETJMP_THUNKS.clear()

    def test_a_call_through_the_iat_becomes_an_inline_setjmp(self):
        recomp.IAT[0x0067F000] = ("MSVCR71.dll", "_setjmp3")
        c = translate([
            ins(0x00401000, "CALL", "CALL dword ptr [0x0067f000]", n=6),
            ins(0x00401006, "RET", "RET", n=1),
        ])
        self.assertIn("x86_setjmp_buf(C)", c)
        self.assertIn("x86_setjmp_done(C, _sj)", c)
        self.assertNotIn("imp_MSVCR71__setjmp3(C);", c,
                         "the stub must NOT be called: its frame cannot be "
                         "resumed into")

    def test_a_call_to_the_jmp_thunk_becomes_an_inline_setjmp(self):
        """MSVC calls the import through a one-instruction thunk, so the call
        site reads `CALL 0x0067281a` and nothing in the body mentions the
        import at all. Missing this made the whole feature a silent no-op."""
        recomp.IAT[0x0067F000] = ("MSVCR71.dll", "_setjmp3")
        thunk = {"ep": 0x0067281A, "thunk": True, "ins": [
            ins(0x0067281A, "JMP", "JMP dword ptr [0x0067f000]", n=6, ind=1)]}
        found = recomp.find_setjmp_thunks([thunk])
        self.assertEqual(found, {0x0067281A},
                         "the thunk that forwards to _setjmp3 must be found")
        c = translate([
            ins(0x00401000, "CALL", "CALL 0x0067281a", n=5, flow=0x0067281A),
            ins(0x00401005, "RET", "RET", n=1),
        ])
        self.assertIn("x86_setjmp_buf(C)", c)

    def test_a_thunk_to_anything_else_is_not_mistaken_for_setjmp(self):
        """The negative case. Run against a thunk forwarding to a DIFFERENT
        import, the detector must find nothing -- a detector only ever run on
        its positive case has not been shown to discriminate."""
        recomp.IAT[0x0067F004] = ("MSVCR71.dll", "malloc")
        thunk = {"ep": 0x00672900, "thunk": True, "ins": [
            ins(0x00672900, "JMP", "JMP dword ptr [0x0067f004]", n=6, ind=1)]}
        self.assertEqual(recomp.find_setjmp_thunks([thunk]), set())


class StackOperandsMoveEspAtTheRightMoment(unittest.TestCase):
    """PUSH reads before ESP moves; POP writes after it moves.

    Only operands BASED ON ESP can tell the difference, which is why this went
    unnoticed: every register push is identical either way. XMen2.exe
    0x0065e314 is four instructions long and passed the wrong stack slot to
    malloc because of it."""

    def test_push_of_an_esp_relative_operand_reads_the_original_esp(self):
        c = translate([
            ins(0x00401000, "PUSH", "PUSH dword ptr [ESP + 0x8]", n=4),
            ins(0x00401004, "RET", "RET", n=1),
        ])
        body = c[c.index("00401000"):c.index("00401004")]
        self.assertNotIn("WR32(C->esp, RD32((uint32_t)(C->esp + 0x8U)))", body,
                         "the operand must not be read after ESP has moved")
        # the read has to happen before the decrement, in source order
        self.assertLess(body.index("RD32"), body.index("C->esp -= 4"),
                        "PUSH must read its operand before moving ESP")

    def test_pop_into_an_esp_relative_operand_writes_the_new_esp(self):
        c = translate([
            ins(0x00401000, "POP", "POP dword ptr [ESP + 0x4]", n=4),
            ins(0x00401004, "RET", "RET", n=1),
        ])
        body = c[c.index("00401000"):c.index("00401004")]
        self.assertLess(body.index("C->esp += 4"), body.index("WR32"),
                        "POP must move ESP before computing the destination")

    def test_pushing_esp_itself_pushes_the_value_before_the_decrement(self):
        c = translate([
            ins(0x00401000, "PUSH", "PUSH ESP", n=1),
            ins(0x00401001, "RET", "RET", n=1),
        ])
        body = c[c.index("00401000"):c.index("00401001")]
        self.assertLess(body.index("C->esp;"), body.index("C->esp -= 4"),
                        "PUSH ESP stores the value ESP had before the push")


class Refusals(unittest.TestCase):
    """The translator's central rule: what it does not understand must fail
    loudly by name, never become a no-op."""

    def test_unknown_mnemonic_aborts_by_name_rather_than_vanishing(self):
        c = translate([
            ins(0x00401000, "VFMADD213PS", "VFMADD213PS XMM0,XMM1,XMM2", n=5),
            ins(0x00401005, "RET", "RET", n=1),
        ])
        self.assertIn("x86_unsupported_insn(", c)
        self.assertIn("NOT TRANSLATED", c)


if __name__ == "__main__":
    unittest.main(verbosity=2)
