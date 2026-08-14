#!/usr/bin/env python3
import os
import sys
import unittest

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "tools"))
from recomp_host_call import host_call_bridge


class HostedCallBridge(unittest.TestCase):
    def test_preserves_both_halves_of_msvc_int64_return(self):
        text = host_call_bridge(False)
        self.assertIn('"=a"(eax), "=d"(edx)', text)
        self.assertIn("C->eax = eax;", text)
        self.assertIn("C->edx = edx;", text)
        self.assertNotIn(': "ecx", "edx"', text)

    def test_runner_mode_brackets_real_x87_return_stack(self):
        text = host_call_bridge(True)
        self.assertLess(text.index("x87_host_begin(C)"), text.index("call *"))
        self.assertLess(text.index("call *"), text.index("x87_host_end(C)"))

    def test_dll_mode_does_not_require_runner_x87_helpers(self):
        text = host_call_bridge(False)
        self.assertNotIn("x87_host_begin", text)
        self.assertNotIn("x87_host_end", text)


if __name__ == "__main__":
    unittest.main()
