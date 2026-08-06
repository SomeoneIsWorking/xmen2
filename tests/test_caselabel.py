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
    """Where a jump table ends, decided rather than guessed.

    The tables in this image are adjacent (0x005fb240 and 0x005fb250), so a
    fixed count runs from one into the next; and past the end of the last one
    the "entries" are just instruction bytes (0x24748b56 followed 0x0066d645's
    ten real entries).
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


if __name__ == "__main__":
    unittest.main(verbosity=2)
