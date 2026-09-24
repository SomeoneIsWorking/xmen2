"""The browser console reader keeps a worker's trap and the stack that names it.

Issue #160's trap was found only after both of these held: the reader has to
subscribe to the pthread workers, which appear AFTER it attaches because the
module loads when play is pressed, and it has to keep the stack an error-level
entry carries. Each case below is paired with the event the reader must leave
alone, so a reader that printed a stack on every line, or subscribed to every
event, fails as surely as one that did neither.
"""

import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent / "tools"))
from cdp_console import CONSOLE_EVENTS, adopt_late_target, console_line

TRAP_FRAMES = {
    "callFrames": [
        {"functionName": "$func7500"},
        {"functionName": "$_h"},
        {"functionName": "callUserCallback"},
    ]
}


class RecordingCdp:
    """Only the one call adopt_late_target makes."""

    def __init__(self):
        self.calls = []

    def call(self, method, params=None, session=None, timeout=None):
        self.calls.append((method, session))
        return {}


def attached(session, url):
    return {
        "method": "Target.attachedToTarget",
        "params": {"sessionId": session, "targetInfo": {"type": "worker", "url": url}},
    }


class ConsoleLineTest(unittest.TestCase):
    def test_a_worker_trap_keeps_its_frames(self):
        line = console_line(
            {
                "method": "Log.entryAdded",
                "params": {
                    "entry": {
                        "level": "error",
                        "text": "Uncaught RuntimeError: null function",
                        "stackTrace": TRAP_FRAMES,
                    }
                },
            }
        )
        self.assertEqual(
            line,
            "error   Uncaught RuntimeError: null function"
            " @ $func7500 <- $_h <- callUserCallback",
        )

    def test_an_entry_without_a_stack_is_unchanged(self):
        line = console_line(
            {"method": "Log.entryAdded", "params": {"entry": {"level": "warning", "text": "w"}}}
        )
        self.assertEqual(line, "warning w")

    def test_an_uncaught_exception_is_a_line(self):
        self.assertIn("Runtime.exceptionThrown", CONSOLE_EVENTS)
        line = console_line(
            {
                "method": "Runtime.exceptionThrown",
                "params": {
                    "exceptionDetails": {
                        "text": "Uncaught",
                        "exception": {"description": "RuntimeError: x\n    at f\n    at g"},
                        "stackTrace": TRAP_FRAMES,
                    }
                },
            }
        )
        self.assertEqual(
            line,
            "thrown  RuntimeError: x |     at f |     at g"
            " @ $func7500 <- $_h <- callUserCallback",
        )


class AdoptLateTargetTest(unittest.TestCase):
    def test_a_worker_that_appears_later_is_subscribed(self):
        cdp = RecordingCdp()
        sessions = {"page": "http://page/"}
        self.assertTrue(adopt_late_target(cdp, attached("w1", "http://page/x2native.js"), sessions))
        self.assertEqual(sessions["w1"], "http://page/x2native.js")
        self.assertEqual(cdp.calls, [("Runtime.enable", "w1"), ("Log.enable", "w1")])

    def test_a_known_session_is_not_subscribed_twice(self):
        cdp = RecordingCdp()
        sessions = {"w1": "http://page/x2native.js"}
        self.assertTrue(adopt_late_target(cdp, attached("w1", "http://page/x2native.js"), sessions))
        self.assertEqual(cdp.calls, [])

    def test_a_console_event_is_not_an_attach(self):
        cdp = RecordingCdp()
        sessions = {}
        event = {"method": "Runtime.consoleAPICalled", "params": {"args": []}}
        self.assertFalse(adopt_late_target(cdp, event, sessions))
        self.assertEqual((cdp.calls, sessions), ([], {}))


if __name__ == "__main__":
    unittest.main()
