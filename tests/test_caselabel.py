#!/usr/bin/env python3
"""Unit tests for the switch-case-label decision, tools/ghidra_scripts/caselabel.py.

Run: python3 tests/test_caselabel.py     (also wired in as the `caselabel` ctest)

## Why this file exists

A switch case label that some earlier pass seeded as a FUNCTION is what stops
a container's flow walk: Ghidra will not absorb another function's entry, so
the case block -- and everything that falls through from it -- stays outside
the container permanently. XMen2.exe 0x0066cf4e wired all 10 entries of its
jump table and still ended with a 127-byte hole, because entry 0 (0x0066cf79)
was FUN_0066cf79 (issue #27).

The repair deletes a function object, which is destructive, so the rule that
decides WHEN is the part that must not be wrong. It lives here, free of any
`ghidra` import, so it can be run in CPython in milliseconds instead of only
inside a 40-minute re-lift.

Each test encodes a DECISION and the message that decision prints. The messages
are tested too: this tool's negatives ("kept", "nothing to un-make") are the
outputs a session acts on, and a negative that cannot be told from "never
looked" is the defect this project keeps logging.
"""
import os
import sys
import unittest

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..",
                                "tools", "ghidra_scripts"))
import caselabel                                           # noqa: E402

CONTAINER = 0x0066CF4E
TABLE = 0x0066D645
CASE = 0x0066CF79


def classify(**kw):
    a = {"container": CONTAINER, "container_name": "FUN_0066cf4e",
         "table": TABLE, "target": CASE, "inner_name": "FUN_0066cf79",
         "call_refs": 0}
    a.update(kw)
    return caselabel.classify(**a)


class Absorb(unittest.TestCase):
    """The case this exists for: a default-named function nothing calls."""

    def test_default_named_uncalled_case_label_is_absorbed(self):
        action, msg = classify()
        self.assertEqual(action, "absorb")
        self.assertIn("0066cf79", msg)
        self.assertIn("FUN_0066cf4e", msg)

    def test_the_message_says_it_deletes_a_function(self):
        # Someone reading the log must see that this was destructive.
        _, msg = classify()
        self.assertIn("un-mak", msg.lower())


class Refusals(unittest.TestCase):
    """Both refusals exist because the alternative breaks something real."""

    def test_a_called_target_is_kept(self):
        # A CALL reference means the container is legitimately non-contiguous.
        # Deleting the function would leave its callers dispatching to nothing.
        action, msg = classify(call_refs=3)
        self.assertEqual(action, "keep-called")
        self.assertIn("3", msg)
        self.assertIn("CALL", msg)

    def test_a_named_target_is_kept(self):
        action, msg = classify(inner_name="decode_bitstream")
        self.assertEqual(action, "keep-named")
        self.assertIn("decode_bitstream", msg)

    def test_a_named_target_is_kept_even_with_no_callers(self):
        action, _ = classify(inner_name="decode_bitstream", call_refs=0)
        self.assertEqual(action, "keep-named")

    def test_sub_prefix_counts_as_a_ghidra_default(self):
        action, _ = classify(inner_name="SUB_0066cf79")
        self.assertEqual(action, "absorb")

    def test_a_name_merely_containing_fun_is_not_a_default(self):
        action, _ = classify(inner_name="FUN_TABLE_get")
        self.assertEqual(action, "keep-named")


class NotApplicable(unittest.TestCase):
    def test_a_target_that_is_not_a_function_is_left_alone(self):
        # Nothing to delete: the flow walk will reach it by itself.
        action, _ = classify(inner_name=None)
        self.assertEqual(action, "not-a-function")

    def test_a_switch_jumping_to_its_own_entry_is_not_un_made(self):
        # Deleting the container mid-recreate destroys the thing being repaired.
        action, _ = classify(target=CONTAINER, inner_name="FUN_0066cf4e")
        self.assertEqual(action, "skip-self")


class Summary(unittest.TestCase):
    """The negative must carry its denominator and its blind spot.

    "absorbed nothing" and "never looked" have to be different lines, or the
    next session reads a table that was never read as a table with no case
    labels.
    """

    def test_no_entries_wired_says_nothing_could_be_examined(self):
        msg = caselabel.summarize(TABLE, cases=0, unmade=0, kept=0)
        self.assertIn("no entries", msg)
        self.assertIn("could be examined", msg)
        self.assertNotIn("nothing to un-make", msg)

    def test_entries_but_no_functions_says_so_with_the_count(self):
        msg = caselabel.summarize(TABLE, cases=10, unmade=0, kept=0)
        self.assertIn("10", msg)
        self.assertIn("nothing to un-make", msg)

    def test_a_positive_result_reports_both_counts(self):
        msg = caselabel.summarize(TABLE, cases=10, unmade=1, kept=2)
        self.assertIn("1", msg)
        self.assertIn("2", msg)
        self.assertIn("10", msg)

    def test_every_summary_names_the_table(self):
        for cases, unmade, kept in ((0, 0, 0), (10, 0, 0), (10, 1, 2)):
            self.assertIn("0066d645",
                          caselabel.summarize(TABLE, cases, unmade, kept))


class TableLength(unittest.TestCase):
    """Where a jump table ends, when only plausibility is available.

    Past the last table the "entries" are instruction bytes -- 0x0066d645's ten
    real entries are followed by 0x24748b56. This test is about that stop.

    What it CANNOT do is separate two adjacent tables, and the comment in
    RecreateFunction.py claimed for weeks that it could. 0x005fb240 (4 entries)
    is immediately followed by 0x005fb250 (7 entries) and every one of the
    eleven dwords is a valid in-block code address, so this heuristic reads
    straight through. See TableBound for what actually decides it.
    """

    def test_a_target_outside_the_executable_block_ends_the_table(self):
        self.assertFalse(caselabel.plausible_entry(
            0x24748B56, in_same_block=False, is_executable=False))

    def test_a_target_in_another_block_ends_the_table(self):
        self.assertFalse(caselabel.plausible_entry(
            0x00401000, in_same_block=False, is_executable=True))

    def test_a_target_in_the_same_executable_block_continues_it(self):
        self.assertTrue(caselabel.plausible_entry(
            0x0066D043, in_same_block=True, is_executable=True))

    def test_it_cannot_see_the_boundary_between_two_adjacent_tables(self):
        # 0x005fb250 is the second table's first entry and is a perfectly good
        # in-block code address. Recorded as a test so nobody re-derives the
        # blind spot from the failure it causes.
        self.assertTrue(caselabel.plausible_entry(
            0x005FAFC1, in_same_block=True, is_executable=True))


class TableBound(unittest.TestCase):
    """How many entries a table REALLY has: the switch's own range check.

    MSVC guards a jump table with `CMP <index>, N` / `JA <default>` immediately
    before `JMP [<index>*4 + <table>]`, so the entry count is N + 1 -- a fact
    read out of the code rather than a guess about where the data ends. Both
    worked examples below are transcribed from XMen2.exe.
    """

    FIRST = ["MOV byte ptr [EDI + 0x4],0x1",
             "MOV EAX,[0x0067f698]",
             "MOV ECX,dword ptr [EAX]",
             "MOV EAX,[0x00a68c98]",
             "CMP EAX,0x3",
             "MOV ECX,dword ptr [ECX + 0x38]",
             "MOV byte ptr [ESP + 0x78],0x2",
             "JA 0x005fad18"]

    SECOND = ["MOV EAX,dword ptr [ESP + 0x4c]",
              "AND EAX,0x7",
              "CMP EAX,0x6",
              "JA 0x005fb03f"]

    def test_the_first_table_bounds_at_four(self):
        n, why = caselabel.table_bound("EAX", self.FIRST)
        self.assertEqual(n, 4)
        self.assertIn("CMP EAX,0x3", why)

    def test_the_second_table_bounds_at_seven(self):
        n, why = caselabel.table_bound("EAX", self.SECOND)
        self.assertEqual(n, 7)

    def test_the_two_bounds_together_do_not_overlap(self):
        # The whole point: 4 + 7 = the 11 dwords the plausibility scan reads as
        # one table.
        a, _ = caselabel.table_bound("EAX", self.FIRST)
        b, _ = caselabel.table_bound("EAX", self.SECOND)
        self.assertEqual(a + b, 11)

    def test_no_conditional_jump_before_the_table_jump_is_no_bound(self):
        # 0x0066cf4e: the JMP is the first instruction of the function; its
        # range check is in the caller. The answer must be "I cannot tell",
        # not a number.
        n, why = caselabel.table_bound("EAX", [])
        self.assertIsNone(n)
        self.assertIn("no range check", why)

    def test_a_bound_on_a_different_register_is_not_this_switch_s(self):
        n, why = caselabel.table_bound(
            "ECX", ["CMP EAX,0x3", "JA 0x005fad18"])
        self.assertIsNone(n)
        self.assertIn("ECX", why)

    def test_an_intervening_write_to_the_index_register_voids_the_bound(self):
        # CMP EAX,3 no longer describes the value the JMP indexes with.
        n, why = caselabel.table_bound(
            "EAX", ["CMP EAX,0x3", "MOV EAX,dword ptr [ESI]", "JA 0x1000"])
        self.assertIsNone(n)
        self.assertIn("MOV EAX", why)

    def test_a_read_of_the_index_register_does_not_void_the_bound(self):
        n, _ = caselabel.table_bound(
            "EAX", ["CMP EAX,0x3", "MOV ECX,dword ptr [EAX]", "JA 0x1000"])
        self.assertEqual(n, 4)

    def test_jbe_is_not_the_shape_and_is_not_guessed_at(self):
        # The inverted form jumps TO the table path; reading its immediate as a
        # count would be off by more than one.
        n, why = caselabel.table_bound("EAX", ["CMP EAX,0x3", "JBE 0x1000"])
        self.assertIsNone(n)
        self.assertIn("JBE", why)

    def test_a_single_case_table_is_one_entry_not_zero(self):
        n, _ = caselabel.table_bound("EAX", ["CMP EAX,0x0", "JA 0x1000"])
        self.assertEqual(n, 1)


class Absorbed(unittest.TestCase):
    """Un-making a case label is only half the repair.

    0x005fafc1 was un-made as a case label of the table at 0x005fb240 -- it was
    read out of that table, because the read had run past the table's end into
    0x005fb250. It really is a case label, but of a switch in ANOTHER function,
    so the container being re-created could not absorb it and it became an
    orphan in no function at all. The runtime then dispatched to it and found
    no body: strictly worse than before the repair.

    So the verdict on a repair is not "did it un-make something" but "is every
    label it un-made now inside the body".
    """

    def test_all_absorbed_is_a_pass(self):
        ok, msg = caselabel.check_absorbed([0x0066CF79], [0x0066CF79])
        self.assertTrue(ok)
        self.assertIn("1", msg)

    def test_an_orphan_fails_and_is_named(self):
        ok, msg = caselabel.check_absorbed([0x005FAFC1], [])
        self.assertFalse(ok)
        self.assertIn("005fafc1", msg)
        self.assertIn("ORPHAN", msg.upper())

    def test_nothing_un_made_is_not_reported_as_success(self):
        ok, msg = caselabel.check_absorbed([], [])
        self.assertTrue(ok)
        self.assertIn("no case label", msg)


class MergePrecondition(unittest.TestCase):
    """A merge only makes sense on a body that was CUT OFF.

    MergeTruncated.py printed "0x0066ced2 ends at 0066cf4a without a
    terminator" about a body whose last instruction is a RET, absorbed the
    3-byte padding function after it, re-created a body of exactly the same
    length, and reported "1 repaired". Nothing was repaired. The claim in that
    message was never checked -- it was asserted.
    """

    def test_a_ret_ends_a_function(self):
        self.assertTrue(caselabel.is_terminator("RET"))

    def test_an_unconditional_jump_ends_a_function(self):
        # A tail call: the body is complete, control leaves and does not return.
        self.assertTrue(caselabel.is_terminator("JMP"))

    def test_a_conditional_jump_does_not_end_a_function(self):
        self.assertFalse(caselabel.is_terminator("JZ"))

    def test_an_ordinary_instruction_does_not_end_a_function(self):
        self.assertFalse(caselabel.is_terminator("MOV"))

    def test_a_body_that_grew_is_a_repair(self):
        ok, msg = caselabel.merge_outcome("FUN_0066cf24", 34, 52)
        self.assertTrue(ok)
        self.assertIn("52", msg)

    def test_a_body_that_did_not_grow_is_NOT_a_repair(self):
        ok, msg = caselabel.merge_outcome("FUN_0066cf4b", 52, 52)
        self.assertFalse(ok)
        self.assertIn("FUN_0066cf4b", msg)
        self.assertIn("did not grow", msg)

    def test_a_body_that_shrank_is_reported_as_worse(self):
        ok, msg = caselabel.merge_outcome("FUN_x", 52, 40)
        self.assertFalse(ok)
        self.assertIn("SHRANK", msg.upper())


if __name__ == "__main__":
    unittest.main(verbosity=2)
