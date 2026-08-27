#!/usr/bin/env python3
"""Pin the shipping Load Game projection and exact-leaf transaction wiring."""

from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[1]


class LoadGameMenuWiringTest(unittest.TestCase):
    def test_runtime_owns_the_three_binary_seams(self) -> None:
        source = (ROOT / "src/native/load_game_menu_runtime.c").read_text()
        for address in ("0x004b0d20u", "0x005e9d30u", "0x0049f010u"):
            self.assertIn(
                f'x86_register_override("XMen2.exe", {address}', source
            )

        build = source[source.index("static void x2_override_004b0d20(") :]
        self.assertLess(
            build.index("fn_XMen2_004b0d20(C)"),
            build.index("activate_projection(C, manager)"),
        )
        self.assertIn("PAGE_COUNT = 0x1558u", source)
        self.assertIn("PAGE_FOCUS = 0x155bu", source)
        self.assertIn("X2_LOAD_GAME_VISIBLE_ROWS", source)

    def test_autosave_bypasses_the_numeric_manager_choice(self) -> None:
        source = (ROOT / "src/native/load_game_menu_runtime.c").read_text()
        start = source.index("static void x2_override_0049f010(")
        end = source.index("size_t x2_load_game_menu_runtime_report(", start)
        choose = source[start:end]

        self.assertIn("selection != AUTOSAVE_SCRIPT_SLOT", choose)
        self.assertIn("fn_XMen2_0049f010(C)", choose)
        self.assertIn("X2_EXACT_SAVE_LOAD_MENU", choose)
        self.assertIn("AUTOSAVE_LEAF, 0u", choose)
        self.assertLess(
            choose.index("selection != AUTOSAVE_SCRIPT_SLOT"),
            choose.index("fn_XMen2_0049f010(C)"),
        )

    def test_continue_and_menu_share_one_exact_leaf_reader(self) -> None:
        exact = (ROOT / "src/native/exact_save_load.c").read_text()
        start = exact.index("int x2_exact_save_load_start(")
        end = exact.index("static int redirect_pending_load(", start)
        transaction = exact[start:end]
        self.assertLess(
            transaction.index(
                "set_manager_mode(source, exe, manager, SAVE_MODE_LOAD)"
            ),
            transaction.index("read_prepared_header(source, exe, entry)"),
        )
        self.assertIn(
            "owner == X2_EXACT_SAVE_LOAD_CONTINUE", transaction
        )
        self.assertIn(
            "set_manager_mode(source, exe, manager, SAVE_MODE_IDLE)",
            transaction,
        )

        continuation = (ROOT / "src/native/continue_runtime.c").read_text()
        self.assertIn("X2_EXACT_SAVE_LOAD_CONTINUE", continuation)
        self.assertIn(
            "X2_EXACT_SAVE_LOAD_CONTINUE,\n"
            "                                  continue_load_completed",
            continuation,
        )
        self.assertNotIn("x2_exact_save_load_redirect", continuation)
        self.assertNotIn("x2_override_0055ff00", continuation)
        redirect = exact[exact.index("static int redirect_pending_load(") :]
        self.assertIn(
            'x86_register_override("XMen2.exe", 0x0055ff00u', redirect
        )
        self.assertIn("fn_XMen2_0055ff00(C)", redirect)
        self.assertIn("if (completion) completion(succeeded)", redirect)
        self.assertLess(
            redirect.index("WR32(cpu->esp + 8u, g_leaf_guest)"),
            redirect.index("x86_dispatch(cpu, exe + FN_READ_LEAF)"),
        )

        menu = (ROOT / "src/native/load_game_menu_runtime.c").read_text()
        self.assertIn("X2_EXACT_SAVE_LOAD_MENU, NULL", menu)

    def test_corrupt_manual_headers_keep_their_retail_row(self) -> None:
        runtime = (ROOT / "src/native/load_game_menu_runtime.c").read_text()
        present = runtime[
            runtime.index("static uint16_t manual_present_mask(") :
            runtime.index("static void make_autosave_row(")
        ]
        self.assertIn("METADATA_EMPTY", present)
        self.assertNotIn("METADATA_INVALID", present)

    def test_live_report_contains_projection_and_manager_selection(self) -> None:
        runtime = (ROOT / "src/native/load_game_menu_runtime.c").read_text()
        self.assertIn("load-menu logical=%zu resident=%zu", runtime)
        self.assertIn("manager-selected=%d", runtime)

        report = (ROOT / "src/native/save_trace_runtime.c").read_text()
        self.assertIn("x2_load_game_menu_runtime_report(", report)
        self.assertIn("out + combined_size, capacity - combined_size", report)
        self.assertNotIn("out + combined_size - 1u", report)


if __name__ == "__main__":
    unittest.main()
