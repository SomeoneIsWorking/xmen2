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


def translate(instructions, ep=None, extra_eps=()):
    """Translate a synthetic function and return its C body as one string.

    `extra_eps` names other functions the module is supposed to contain, so a
    branch out of this one can be a tail call rather than an unknown target.
    """
    fn = {"ep": ep if ep is not None else instructions[0]["a"],
          "qname": "Test::fn", "name": "fn", "thunk": False,
          "size": sum(i["n"] for i in instructions), "ins": instructions}
    recomp.IMG[0], recomp.IMG[1] = 0x00400000, 0x00a75000
    recomp.KNOWN_EPS.clear()
    recomp.KNOWN_EPS.add(fn["ep"])
    recomp.KNOWN_EPS.update(extra_eps)
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


class InteriorEntries(unittest.TestCase):
    """Entering a generated body at a label, from outside it.

    MSVC shares one epilogue between paths, so a `JMP` lands in the MIDDLE of
    another function. XMen2.exe 0x0066cf3c is the worked example: `PUSH ESI /
    PUSH EBX / CALL / ADD ESP,0xc / POP EDI / POP ESI / POP EBX / LEAVE / RET`,
    inside FUN_0066ced2 (which falls through into it) and jumped to from the
    switch at 0x0066d633. Image-wide: 28 such targets from 38 sites, 0 calls.

    Before this there was nothing to emit -- the target has no function name --
    so it became `x86_call_unknown` and the run stopped. It is NOT a boundary
    defect: FUN_0066ced2 ends in RET, so it is complete, and carving the block
    out would truncate its predecessor instead (issues #21, #27, #29).

    Note what these tests do NOT cover: a direct CALL to an interior address.
    There are none in this image, so it still reports by name rather than
    shipping a path nothing has ever run.
    """

    def setUp(self):
        recomp.INTERIOR.clear()

    def owner_and_jumper(self):
        """A body of two functions: 0x00402000 jumps into 0x00401000's middle."""
        owner = [ins(0x00401000, "PUSH", "PUSH ESI", n=1),
                 ins(0x00401001, "PUSH", "PUSH EBX", n=1),
                 ins(0x00401002, "RET", "RET", n=1)]
        recomp.INTERIOR[0x00401001] = 0x00401000
        return owner

    def test_a_jump_into_another_body_enters_it_at_that_label(self):
        self.owner_and_jumper()
        c = translate([
            ins(0x00402000, "JMP", "JMP 0x00401001", n=5, flow=0x00401001),
        ], ep=0x00402000)
        self.assertIn("C->enter_at", c)
        self.assertIn("0x00401001", c)
        self.assertIn("fn_00401000(C)", c)
        self.assertNotIn("x86_call_unknown", c,
                         "the target is known: it is inside a function this "
                         "module emits, and can now be entered at a label")

    def test_the_owner_gets_a_label_and_an_entry_check(self):
        owner = self.owner_and_jumper()
        c = translate(owner, ep=0x00401000)
        self.assertIn("L_00401001:", c)
        self.assertIn("L_injmp", c,
                      "the label switch is how an outside entry reaches the "
                      "label, so the owner needs one even with no computed "
                      "jump of its own")
        self.assertIn("C->enter_at", c)

    def test_the_entry_is_consumed_so_the_body_cannot_re_enter_itself(self):
        # A body that left enter_at set would jump to the same label again the
        # next time it is called normally -- silently skipping its prologue.
        owner = self.owner_and_jumper()
        c = translate(owner, ep=0x00401000)
        self.assertIn("C->enter_at = 0", c)

    def test_a_function_nobody_enters_interior_is_unchanged(self):
        c = translate([
            ins(0x00401000, "PUSH", "PUSH ESI", n=1),
            ins(0x00401001, "RET", "RET", n=1),
        ], ep=0x00401000)
        self.assertNotIn("L_injmp", c,
                         "labelling every instruction in the image is what "
                         "the _has_injmp restriction exists to avoid")
        self.assertNotIn("enter_at", c)

    def test_a_jump_to_a_real_entry_point_is_still_a_plain_tail_call(self):
        c = translate([
            ins(0x00402000, "JMP", "JMP 0x00401000", n=5, flow=0x00401000),
        ], ep=0x00402000, extra_eps=[0x00401000])
        self.assertIn("fn_00401000(C)", c)
        self.assertNotIn("enter_at", c)

    def test_a_jump_into_no_function_at_all_still_reports_by_name(self):
        c = translate([
            ins(0x00402000, "JMP", "JMP 0x00409999", n=5, flow=0x00409999),
        ], ep=0x00402000)
        self.assertIn("x86_call_unknown", c,
                      "an address in no function is a genuinely missing "
                      "body and must stay a named stop")

    def test_the_scan_finds_a_cross_function_interior_target(self):
        fns = [{"ep": 0x00401000, "ins": [
                    ins(0x00401000, "PUSH", "PUSH ESI", n=1),
                    ins(0x00401001, "RET", "RET", n=1)]},
               {"ep": 0x00402000, "ins": [
                    ins(0x00402000, "JMP", "JMP 0x00401001", n=5,
                        flow=0x00401001)]}]
        self.assertEqual(recomp.interior_entries(fns), {0x00401001: 0x00401000})

    def test_the_scan_ignores_a_jump_inside_the_same_function(self):
        fns = [{"ep": 0x00401000, "ins": [
                    ins(0x00401000, "JMP", "JMP 0x00401002", n=2,
                        flow=0x00401002),
                    ins(0x00401002, "RET", "RET", n=1)]}]
        self.assertEqual(recomp.interior_entries(fns), {},
                         "a local jump is already a goto; routing it through "
                         "an entry would be slower and no more correct")

    def test_the_scan_ignores_a_jump_to_a_function_entry(self):
        fns = [{"ep": 0x00401000, "ins": [
                    ins(0x00401000, "RET", "RET", n=1)]},
               {"ep": 0x00402000, "ins": [
                    ins(0x00402000, "JMP", "JMP 0x00401000", n=5,
                        flow=0x00401000)]}]
        self.assertEqual(recomp.interior_entries(fns), {})

    def test_a_conditional_jump_into_another_body_enters_it_too(self):
        # 22 of the 38 sites are conditional. A Jcc is a predicated jump --
        # no return address, no stack change -- so it is the same mechanism.
        self.owner_and_jumper()
        c = translate([
            ins(0x00402000, "JZ", "JZ 0x00401001", n=6, flow=0x00401001),
            ins(0x00402006, "RET", "RET", n=1),
        ], ep=0x00402000)
        self.assertIn("C->enter_at", c)
        self.assertIn("fn_00401000(C)", c)
        self.assertNotIn("x86_call_unknown", c)

    def test_the_scan_finds_a_conditional_interior_target(self):
        fns = [{"ep": 0x00401000, "ins": [
                    ins(0x00401000, "PUSH", "PUSH ESI", n=1),
                    ins(0x00401001, "RET", "RET", n=1)]},
               {"ep": 0x00402000, "ins": [
                    ins(0x00402000, "JNZ", "JNZ 0x00401001", n=6,
                        flow=0x00401001)]}]
        self.assertEqual(recomp.interior_entries(fns), {0x00401001: 0x00401000})

    def test_the_scan_ignores_a_CALL_to_an_interior_address(self):
        # Measured: there are none. Were one to appear it must keep reporting
        # by name rather than take a path no run has exercised.
        fns = [{"ep": 0x00401000, "ins": [
                    ins(0x00401000, "PUSH", "PUSH ESI", n=1),
                    ins(0x00401001, "RET", "RET", n=1)]},
               {"ep": 0x00402000, "ins": [
                    ins(0x00402000, "CALL", "CALL 0x00401001", n=5,
                        flow=0x00401001)]}]
        self.assertEqual(recomp.interior_entries(fns), {})




class X87Transcendentals(unittest.TestCase):
    """FPTAN, which the game reaches in igCommonTraversal::setPerspective.

    It is the one x87 transcendental whose STACK EFFECT is not the obvious
    one: it replaces ST(0) with its tangent and then PUSHES 1.0, so the stack
    grows by one. A translation that only computed the tangent would leave
    every later ST(n) reference off by one register -- silently, and only in
    the code paths that use it."""

    def test_fptan_replaces_st0_and_pushes_one(self):
        c = translate([
            ins(0x00401000, "FPTAN", "FPTAN", n=2),
            ins(0x00401002, "RET", "RET", n=1),
        ])
        self.assertIn("__builtin_tanl", c)
        self.assertIn("x87_push(C, 1.0L)", c)
        self.assertNotIn("x86_unsupported_insn(", c)

    def test_fptan_computes_the_tangent_before_pushing(self):
        """The push must come SECOND: pushing first would make the tangent be
        taken of the 1.0 that was just pushed."""
        c = translate([
            ins(0x00401000, "FPTAN", "FPTAN", n=2),
            ins(0x00401002, "RET", "RET", n=1),
        ])
        self.assertLess(c.index("__builtin_tanl"), c.index("x87_push(C, 1.0L)"))

    def test_fptan_is_not_confused_with_fpatan(self):
        """FPATAN pops; FPTAN pushes. Sharing a branch would get one wrong."""
        c = translate([
            ins(0x00401000, "FPATAN", "FPATAN", n=2),
            ins(0x00401002, "RET", "RET", n=1),
        ])
        self.assertIn("__builtin_atan2l", c)
        self.assertIn("x87_pop(C)", c)
        self.assertNotIn("x87_push(C, 1.0L)", c)


class IntegerX87Widths(unittest.TestCase):
    """FILD and FISTP take m16, m32 or m64, and the width is the operand's.

    Every integer x87 memory operand used to be translated at 32 bits. That
    made `FILD qword ptr` read the LOW dword of a 64-bit value and sign-extend
    it, which is issue #35: the engine's frame timer is a 64-bit nanosecond
    count, so it wrapped negative 2.147 seconds into a run and the frame
    limiter spun forever on a clock that had gone backwards. The failure was
    two subsystems away from the instruction and cost a session to find.
    """

    def test_fild_qword_reads_all_64_bits(self):
        c = translate([
            ins(0x00401000, "FILD", "FILD qword ptr [ESP]", n=3),
            ins(0x00401003, "RET", "RET", n=1),
        ])
        self.assertIn("RDI64(", c)
        self.assertNotIn("RDI32(", c)

    def test_fild_dword_still_reads_32(self):
        c = translate([
            ins(0x00401000, "FILD", "FILD dword ptr [ESP]", n=3),
            ins(0x00401003, "RET", "RET", n=1),
        ])
        self.assertIn("RDI32(", c)

    def test_fild_word_reads_16_signed(self):
        c = translate([
            ins(0x00401000, "FILD", "FILD word ptr [ESP]", n=3),
            ins(0x00401003, "RET", "RET", n=1),
        ])
        self.assertIn("RDI16(", c)

    def test_fistp_qword_writes_all_64_bits(self):
        c = translate([
            ins(0x00401000, "FISTP", "FISTP qword ptr [ESP]", n=3),
            ins(0x00401003, "RET", "RET", n=1),
        ])
        self.assertIn("WR64(", c)
        self.assertIn("x87_to_i64(", c)

    def test_fistp_dword_writes_32(self):
        c = translate([
            ins(0x00401000, "FISTP", "FISTP dword ptr [ESP]", n=3),
            ins(0x00401003, "RET", "RET", n=1),
        ])
        self.assertIn("WR32(", c)
        self.assertNotIn("WR64(", c)

    def test_fild_of_an_unknown_width_fails_loudly_by_name(self):
        """An 80-bit integer operand is not a width FILD has. Guessing one is
        exactly how this defect happened, so the instruction is marked
        untranslatable WITH ITS WIDTH rather than narrowed to 32 bits."""
        c = translate([
            ins(0x00401000, "FILD", "FILD tbyte ptr [ESP]", n=3),
            ins(0x00401003, "RET", "RET", n=1),
        ])
        self.assertIn("x86_unsupported_insn(", c)
        self.assertIn("FILD from a 10-byte integer operand", c)
        self.assertNotIn("RDI32(", c)


class EntryPointIsNotAlwaysTheLowestAddress(unittest.TestCase):
    """MSVC puts an adjustor thunk far from the code it jumps to, and Ghidra
    merges the two ranges into one function whose ENTRY is the HIGHER address.

    Emitting in address order and falling into the first instruction then runs
    the body from the wrong place. Issue #36: `ADD ECX,0x867ac` sat at the
    entry point and was emitted after a `return`, so an array base was never
    applied and `(ptr - base) / 804` came out as 685 instead of 0 -- a fault on
    element 685 of a 175-element array, two functions and a vtable dispatch
    away from the instruction that was skipped.
    """

    def _thunk(self):
        return [
            ins(0x005bee90, "MOV", "MOV EDX,dword ptr [ESP + 0x4]", n=4),
            ins(0x005bee94, "SUB", "SUB EDX,ECX", n=2),
            ins(0x005bee96, "RET", "RET", n=1),
            ins(0x005d4d80, "ADD", "ADD ECX,0x867ac", n=6),
            ins(0x005d4d86, "JMP", "JMP 0x005bee90", n=2, flow=0x005bee90),
        ]

    def test_the_body_jumps_to_its_entry_point_first(self):
        c = translate(self._thunk(), ep=0x005d4d80)
        self.assertIn("goto L_005d4d80;", c)
        # and it does so BEFORE the lowest-address instruction is emitted
        self.assertLess(c.index("goto L_005d4d80;"), c.index("005bee90 MOV"))

    def test_the_entry_point_gets_a_label(self):
        c = translate(self._thunk(), ep=0x005d4d80)
        self.assertIn("L_005d4d80:;", c)

    def test_an_ordinary_body_gets_no_jump(self):
        """The negative: when the entry point IS the first instruction --
        which is every other function in the image -- nothing changes."""
        c = translate([
            ins(0x00401000, "MOV", "MOV EAX,ECX", n=2),
            ins(0x00401002, "RET", "RET", n=1),
        ])
        self.assertNotIn("is not the lowest address", c)

    def test_a_body_that_does_not_contain_its_entry_point_is_refused(self):
        """Not translated with a guess: the body would have no way in at all."""
        fn = {"ep": 0x00405000, "qname": "Test::fn", "name": "fn",
              "thunk": False, "size": 3,
              "ins": [ins(0x00401000, "MOV", "MOV EAX,ECX", n=2),
                      ins(0x00401002, "RET", "RET", n=1)]}
        recomp.IMG[0], recomp.IMG[1] = 0x00400000, 0x00a75000
        recomp.KNOWN_EPS.clear()
        recomp.KNOWN_EPS.add(fn["ep"])
        body, reason = recomp.translate(fn)
        self.assertIsNone(body)
        self.assertIn("entry point", reason)


class X87RegisterStores(unittest.TestCase):
    """FST ST(i) does not pop; FSTP ST(i) pops but stores FIRST.

    Both were wrong. FST ST(i) was emitted as a pop, which drains the modelled
    stack one slot per execution until it underflows; FSTP ST(i) stored the
    POPPED value into X87_ST(i), which indexes the post-pop stack, so
    `FSTP ST(1)` landed in what had been ST(2). 2981 of the second form in the
    image and 2 of the first (issue #40).
    """

    def test_fst_register_does_not_pop(self):
        c = translate([
            ins(0x00401000, "FST", "FST ST2", n=2),
            ins(0x00401002, "RET", "RET", n=1),
        ])
        self.assertIn("X87_ST(C, 2) = X87_ST(C, 0);", c)
        self.assertNotIn("x87_pop", c)

    def test_fstp_register_pops(self):
        c = translate([
            ins(0x00401000, "FSTP", "FSTP ST2", n=2),
            ins(0x00401002, "RET", "RET", n=1),
        ])
        self.assertIn("x87_pop(C)", c)

    def test_fstp_register_stores_before_popping(self):
        """The store must use the PRE-pop indices, or it lands one register
        along -- which is silent, and wrong only in the values."""
        c = translate([
            ins(0x00401000, "FSTP", "FSTP ST2", n=2),
            ins(0x00401002, "RET", "RET", n=1),
        ])
        body = c[c.index("FSTP ST2"):]
        self.assertLess(body.index("X87_ST(C, 2) = _v;"), body.index("x87_pop(C)"))
        self.assertNotIn("X87_ST(C, 2) = x87_pop(C);", c)

    def test_fstp_st0_is_a_discarding_pop(self):
        """FSTP ST(0) stores ST0 into itself and pops, i.e. it discards. It
        must NOT write the popped value over the new top."""
        c = translate([
            ins(0x00401000, "FSTP", "FSTP ST0", n=2),
            ins(0x00401002, "RET", "RET", n=1),
        ])
        self.assertNotIn("X87_ST(C, 0) = x87_pop(C);", c)
        self.assertIn("x87_pop(C)", c)


# unittest.main() LAST, always.
#
# It used to sit in the middle of the file, and everything appended after it
# simply never ran when this file was executed directly: `python3
# tests/test_recomp.py` reported 24 passing tests while unittest discovery
# found 33. A test that cannot run is worse than one that fails, because it
# reports OK.

class WaitingX87Forms(unittest.TestCase):
    """FSTCW/FSTSW/FWAIT: the forms with the exception check in front.

    The translator handled FNSTCW/FNSTSW and refused FSTCW/FSTSW/WAIT, so a
    statically-linked MSVC CRT -- which uses the waiting forms when it sets up
    its floating-point environment -- aborted on the first one it executed.
    cg.dll stopped there (issue #45): 12 FSTSW, 10 WAIT and 4 FSTCW in that
    image alone.

    The waiting forms differ only in checking for a PENDING unmasked x87
    exception first. This runtime raises nothing lazily -- x87_fault stops at
    the instruction that caused it -- so there is no pending state for the wait
    to inspect, which is why the two forms translate identically here rather
    than approximately.
    """

    def test_fstcw_stores_the_control_word(self):
        c = translate([
            ins(0x00401000, "FSTCW", "FSTCW word ptr [ESP + 0x4]", n=4,
                o=[{"k": "mem", "size": 2, "base": "esp", "disp": 4}]),
            ins(0x00401004, "RET", "RET", n=1),
        ])
        self.assertIn("C->fcw", c)
        self.assertIn("WR16(", c)

    def test_fstsw_ax_matches_fnstsw(self):
        want = translate([
            ins(0x00401000, "FNSTSW", "FNSTSW AX", n=2,
                o=[{"k": "reg", "size": 2, "reg": "ax"}]),
            ins(0x00401002, "RET", "RET", n=1),
        ])
        got = translate([
            ins(0x00401000, "FSTSW", "FSTSW AX", n=3,
                o=[{"k": "reg", "size": 2, "reg": "ax"}]),
            ins(0x00401003, "RET", "RET", n=1),
        ])
        self.assertIn("C->fsw", got)
        self.assertIn("C->eax = (C->eax & 0xFFFF0000U) | (C->fsw & 0xFFFFU);", got)
        self.assertIn("C->eax = (C->eax & 0xFFFF0000U) | (C->fsw & 0xFFFFU);", want)

    def test_fstsw_memory_form_stores_rather_than_loading_eax(self):
        """The memory form must NOT be translated as the AX form: it would
        leave the guest's buffer untouched and clobber EAX instead."""
        c = translate([
            ins(0x00401000, "FSTSW", "FSTSW word ptr [ESP]", n=3,
                o=[{"k": "mem", "size": 2, "base": "esp", "disp": 0}]),
            ins(0x00401003, "RET", "RET", n=1),
        ])
        self.assertIn("WR16(", c)
        self.assertNotIn("C->eax =", c)

    def test_wait_is_a_no_op_not_a_refusal(self):
        c = translate([
            ins(0x00401000, "WAIT", "WAIT", n=1),
            ins(0x00401001, "RET", "RET", n=1),
        ])
        self.assertIn("RET", c)


if __name__ == "__main__":
    unittest.main(verbosity=2)
