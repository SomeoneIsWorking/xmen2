"""The browser touch driver's reader of the product's own touch census.

A reader that cannot report the bad run is worth nothing, so each case below
is paired: the census block from a run where touch reached the pad, and the
one from a run where it did not. The falsifier at the end feeds a block the
reader must NOT accept.
"""

import os
import subprocess
import unittest

from tools.web_touch_play import Census

GATE_ACTIVE = (
    "log     [touch] [HB] 32 contact event(s): 16 down, 0 moved, 16 up, 0 canceled",
    "log     [touch] [HB] 0 of 32 dropped before routing: 0 with no window, "
    "0 with the overlay hidden (touch_controls=AUTO, source says touch, gate active)",
    "log     [touch] [HB] 9 zone action(s) routed; 0 cancellation(s) for a lost "
    "window, rotation or layout change",
    "log     [touch] [HB] published to the pad: 14 button change(s) (0 refused), "
    "4 axis change(s) (0 refused)",
    "log     [touch] [HB] the touch pad was claimed for player one",
)

EVERYTHING_DROPPED = (
    "log     [touch] [HB] 32 contact event(s): 16 down, 0 moved, 16 up, 0 canceled",
    "log     [touch] [HB] 32 of 32 dropped before routing: 0 with no window, "
    "32 with the overlay hidden (touch_controls=AUTO, source says touch, gate never-seen)",
    "log     [touch] [HB] 0 zone action(s) routed; 0 cancellation(s) for a lost "
    "window, rotation or layout change",
    "log     [touch] [HB] published to the pad: 0 button change(s) (0 refused), "
    "0 axis change(s) (0 refused)",
    "log     [touch] [HB] a controller chosen in this run already holds player "
    "one (3 time(s) asked), so the touch pad was not claimed for it -- if "
    "nothing moves, THAT controller is what the guest is reading",
)

NOTHING_TOUCHED = (
    "log     [touch] [HB] no contact reached the port this run -- "
    "touch_controls=AUTO, source says not touch, gate never-seen, a window was "
    "present. Nothing was dropped; nothing arrived",
)


def read(lines):
    census = Census()
    for line in lines:
        census.feed(line)
    return census


class CensusReaderTest(unittest.TestCase):
    def test_a_fresh_census_has_said_nothing(self):
        """Not zero -- nothing. A reader that starts at 0 would report a run
        whose heartbeat never printed as a run in which nothing happened."""
        census = Census()
        self.assertIsNone(census.contacts)
        self.assertIsNone(census.gate)
        self.assertIsNone(census.buttons)
        self.assertEqual(census.beats, 0)

    def test_a_run_that_reached_the_pad(self):
        census = read(GATE_ACTIVE)
        self.assertEqual(census.beats, 1)
        self.assertEqual(census.contacts, 32)
        self.assertEqual(census.dropped, 0)
        self.assertEqual(census.gate, "active")
        self.assertEqual(census.source, "touch")
        self.assertEqual(census.mode, "AUTO")
        self.assertEqual(census.zones, 9)
        self.assertEqual((census.buttons, census.buttons_refused), (14, 0))
        self.assertEqual((census.axes, census.axes_refused), (4, 0))
        self.assertEqual(census.player_one, "claimed")

    def test_a_run_whose_contacts_were_all_dropped(self):
        census = read(EVERYTHING_DROPPED)
        self.assertEqual(census.contacts, 32)
        self.assertEqual(census.dropped, 32)
        self.assertEqual(census.gate, "never-seen")
        self.assertEqual(census.zones, 0)
        self.assertEqual(census.buttons, 0)
        self.assertEqual(census.player_one, "held-by-transient")

    def test_the_two_runs_are_told_apart_by_the_gate_and_the_pad(self):
        """The whole point: both saw 32 contacts. Only the gate, the zone
        count and the pad say which one was playable."""
        good, bad = read(GATE_ACTIVE), read(EVERYTHING_DROPPED)
        self.assertEqual(good.contacts, bad.contacts)
        self.assertNotEqual(good.gate, bad.gate)
        self.assertNotEqual(good.buttons, bad.buttons)

    def test_a_run_in_which_nothing_was_touched(self):
        """Distinct from a run whose contacts were dropped: zero arrived."""
        census = read(NOTHING_TOUCHED)
        self.assertEqual(census.beats, 1)
        self.assertEqual(census.contacts, 0)
        self.assertEqual(census.dropped, 0)
        self.assertIsNone(census.zones)
        # The gate is the whole point of this branch: a run waiting through
        # the logo must be able to say the controls are not on screen YET.
        self.assertEqual(census.gate, "never-seen")
        self.assertEqual(census.source, "not touch")

    def test_beats_count_only_the_line_that_opens_a_block(self):
        census = read(GATE_ACTIVE + EVERYTHING_DROPPED + NOTHING_TOUCHED)
        self.assertEqual(census.beats, 3)

    def test_unrelated_console_output_is_not_read_as_a_census(self):
        """The falsifier. Engine heartbeat lines carry the same [HB] tag and
        the same shapes of number; a reader that matched them would invent a
        touch account out of the JIT's."""
        census = read((
            "log     [engine] [HB] JIT: 0 blocks entered (0 re-entered), "
            "0 translated (0 instructions)",
            "log     [engine] [HB] 21 contact event(s): nonsense",
            "error   [touch] a contact arrived with no window",
        ))
        self.assertEqual(census.beats, 0)
        self.assertIsNone(census.contacts)
        self.assertIsNone(census.gate)


PLAYER_ONE_LINES = {
    "claimed": "log     [touch] [HB] the touch pad was claimed for player one",
    "refused": "log     [touch] [HB] the touch pad was REFUSED -- touch cannot "
               "reach gameplay in this run",
    "held-by-transient":
        "log     [touch] [HB] a controller chosen in this run already holds "
        "player one (3 time(s) asked), so the touch pad was not claimed for it",
    "held-by-setting":
        "log     [touch] [HB] a stored controller reservation holds player one "
        "(2 time(s) asked), so the touch pad was not claimed for it -- clear it "
        "in the settings to play by touch",
    "no-slot":
        "log     [touch] [HB] the touch pad had no inventory slot when player "
        "one was asked for (5 time(s)), so nothing was claimed and touch cannot "
        "reach gameplay",
    "never-asked":
        "log     [touch] [HB] player one was never asked for -- no contact "
        "reached the publish path, so this says nothing about who owns the "
        "player",
}


class PlayerOneOutcomeTest(unittest.TestCase):
    """Each way player one can end up, told apart.

    These were one sentence -- "player one already had a controller" -- printed
    whenever no claim and no refusal had been counted, which is also what a run
    that never asked and a run with no pad slot look like. A reader chasing
    "touch does nothing" was sent to look for a controller that in two of those
    three cases did not exist.
    """

    def test_every_outcome_reads_back_as_itself(self):
        for want, line in PLAYER_ONE_LINES.items():
            with self.subTest(outcome=want):
                self.assertEqual(read([line]).player_one, want)

    def test_the_outcomes_are_all_distinct(self):
        got = {read([line]).player_one for line in PLAYER_ONE_LINES.values()}
        self.assertEqual(len(got), len(PLAYER_ONE_LINES))

    def test_a_line_about_some_other_player_one_is_not_an_outcome(self):
        """The falsifier: the words alone must not be enough."""
        census = read([
            "log     [engine] [HB] player one was never asked for",
            "log     [touch] the touch pad was inspected for player one",
        ])
        self.assertIsNone(census.player_one)


class ShippingCensusTest(unittest.TestCase):
    """The reader against the text the product actually prints.

    The fixtures above were typed by hand and can only drift from
    src/input/touch_census.cpp. This runs the C touch test, which ends by
    calling the real x2_touch_runtime_report, and reads its output. If a
    census line is reworded, this fails and the fixtures above are wrong.
    """

    def report_lines(self):
        binary = os.environ.get("X2_TEST_TOUCH_RUNTIME_BINARY")
        if not binary:
            self.fail(
                "X2_TEST_TOUCH_RUNTIME_BINARY is unset. This check reads the "
                "shipping census text from the C test's own report; without "
                "the binary it would pass while checking nothing."
            )
        run = subprocess.run(
            [binary], capture_output=True, text=True,
            env={**os.environ, "SDL_VIDEODRIVER": "dummy", "SDL_AUDIODRIVER": "dummy"},
        )
        if run.returncode == 77:
            self.fail(
                "the C touch test skipped (no SDL video/gamepad here), so it "
                "printed no census. This check cannot run without one."
            )
        self.assertEqual(run.returncode, 0, run.stdout + run.stderr)
        return [ln for ln in (run.stdout + run.stderr).splitlines() if "[touch]" in ln]

    def test_the_reader_parses_the_census_the_product_prints(self):
        lines = self.report_lines()
        self.assertTrue(lines, "the C touch test printed no [touch] lines at all")
        census = read(lines)
        # Two blocks: the C test reports before it touches anything and again
        # at the end, so both branches of the shipping census are checked.
        self.assertEqual(census.beats, 2, f"expected two census blocks in {lines}")
        for field in ("contacts", "dropped", "gate", "source", "mode",
                      "zones", "buttons", "axes", "player_one"):
            self.assertIsNotNone(
                getattr(census, field),
                f"the reader did not find '{field}' in the shipping census: {lines}",
            )
        # The C test drives a run in which touch does reach the pad, so this
        # is the positive class: a reader that matched the words but not the
        # numbers would still leave these at zero.
        self.assertGreater(census.contacts, 0)
        self.assertGreater(census.zones, 0)
        self.assertGreater(census.buttons, 0)
        self.assertEqual(census.gate, "active")
        # Pinned to the real wording, not just "something was read": the C
        # test drives a run in which touch does claim player one.
        self.assertEqual(census.player_one, "claimed")

    def test_the_reader_parses_the_census_branch_that_saw_nothing(self):
        """The first block the C test prints, before it has touched anything.

        Read on its own so the busy block cannot overwrite its fields: this
        branch used to omit the gate entirely, which left a real browser run
        unable to say whether the port had reached gameplay."""
        lines = self.report_lines()
        first = next(ln for ln in lines if "no contact reached the port" in ln)
        census = read([first])
        self.assertEqual(census.beats, 1)
        self.assertEqual(census.contacts, 0)
        self.assertEqual(census.gate, "never-seen")
        self.assertIsNotNone(census.mode)
        self.assertIsNotNone(census.source)


if __name__ == "__main__":
    unittest.main()
