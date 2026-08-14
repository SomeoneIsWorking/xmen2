#!/usr/bin/env python3
import os
import sys
import unittest

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "tools"))
from recomp_hosted import DISPATCH_BODY, import_adapter


class HostedImportAdapters(unittest.TestCase):
    def test_x87_abi_import_uses_emulated_stack_adapter(self):
        text = import_adapter("imp_MSVCR71__CIpow", "MSVCR71.DLL", "_CIpow", 4)
        self.assertEqual(text, "void imp_MSVCR71__CIpow(CPU *C) { x87_crt_cipow(C); }")
        self.assertNotIn("x86_call_host", text)

    def test_ordinary_import_stays_on_host_bridge(self):
        self.assertIsNone(import_adapter(
            "imp_KERNEL32_GetTickCount", "KERNEL32.DLL", "GetTickCount", 8))

    def test_initterm_uses_guest_callback_dispatch(self):
        text = import_adapter(
            "imp_MSVCR71__initterm", "MSVCR71.DLL", "_initterm", 4)
        self.assertIn("x86_host_initterm(C)", text)
        self.assertNotIn("x86_call_host", text)

    def test_termination_import_gets_persistent_diagnostic(self):
        text = import_adapter("imp_KERNEL32_ExitProcess", "KERNEL32.DLL",
                              "ExitProcess", 12)
        self.assertIn("x86_termination_note", text)
        self.assertIn("g_imp[12]", text)


class HostedTailDispatch(unittest.TestCase):
    def test_nested_direct_tail_executes_before_its_caller_resumes(self):
        self.assertIn("C->call_depth == C->dispatch_depth", DISPATCH_BODY)
        self.assertIn("x86_dispatch(C, target);", DISPATCH_BODY)
        self.assertNotIn("g_tail_target", DISPATCH_BODY)

    def test_nested_dispatch_restores_its_callers_state(self):
        self.assertIn("outer_depth = C->dispatch_depth", DISPATCH_BODY)
        self.assertIn("outer_target = C->tail_target", DISPATCH_BODY)
        self.assertIn("C->dispatch_depth = outer_depth", DISPATCH_BODY)
        self.assertIn("C->tail_target = outer_target", DISPATCH_BODY)


if __name__ == "__main__":
    unittest.main()
