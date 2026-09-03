#!/usr/bin/env python3
"""Prove the Continue hooks enter the native-override routing boundary."""

from pathlib import Path
import sys

from check_save_trace_wiring import (WiringError, audit_retail_bodies,
                                     retail_body_call)
from native_overrides import scan_overrides


ROOT = Path(__file__).resolve().parents[1]
RUNTIME = (ROOT / "src" / "native" / "continue_runtime.c").resolve()
EXACT_LOAD = (ROOT / "src" / "native" / "exact_save_load.c").resolve()
STARTUP = (ROOT / "src" / "native" / "startup.c").resolve()
PLAYER = (ROOT / "src" / "native" / "boot_player_selection.c").resolve()
EXPECTED = {0x005C9260, 0x005F2B70, 0x0055FF00, 0x004B1280}


def refuse(message):
    raise WiringError(message)


def scanned_continue_entries():
    expected_by_owner = {
        RUNTIME: EXPECTED - {0x0055FF00},
        EXACT_LOAD: {0x0055FF00},
    }
    found_by_owner = {path: set() for path in expected_by_owner}
    for module, ep, path, _ in scan_overrides(ROOT):
        resolved = Path(path).resolve()
        if module == "XMen2.exe" and resolved in found_by_owner:
            found_by_owner[resolved].add(ep)
    for owner, expected in expected_by_owner.items():
        found = found_by_owner[owner]
        if found == expected:
            continue
        missing = ", ".join(f"0x{ep:08x}" for ep in sorted(expected - found))
        extra = ", ".join(f"0x{ep:08x}" for ep in sorted(found - expected))
        refuse(f"authoritative {owner.name} scan mismatch; missing [{missing}], "
               f"extra [{extra}]")


def audit_success_ack_order(source):
    retained = source.find(retail_body_call(0x004B1280))
    decision = source.find("x2_continue_transaction_take_success_ack(")
    callback = source.find(
        "x86_guest_call_args(&call, g_exe + FN_SUCCESS_CALLBACK, 0u);")
    if min(retained, decision, callback) < 0:
        refuse("native Continue success acknowledgement is missing a retained "
               "body, one-shot decision, or zero-pop callback call")
    if not retained < decision < callback:
        refuse("native Continue success acknowledgement does not retain the "
               "dialog body before taking the one-shot and invoking retail")


def audit_boot_dispatch(runtime_source, startup_source, player_source):
    dispatch = runtime_source.find("void x2_override_005c9260(")
    select = runtime_source.find(
        "x2_boot_player_select_primary(C, PRIMARY_LOCAL_PLAYER)", dispatch)
    show = runtime_source.find(retail_body_call(0x005C9260), dispatch)
    hide = runtime_source.find("g_exe + FN_MAIN_MENU_HIDE", show)
    shared_chain = runtime_source.find(
        "g_exe + FN_CONTINUE_CALLBACK", hide)
    consume = runtime_source.find(
        "x2_boot_mode_runtime_continue_started();", shared_chain)
    if min(dispatch, select, show, hide, shared_chain, consume) < 0 \
            or not dispatch < select < show < hide < shared_chain < consume:
        refuse("boot Continue does not select the primary player before the "
               "retail CMenuMain Show/Hide lifecycle dispatches the "
               "authoritative mode-3 chain")

    # The menu lifecycle between the Show intercept and the LOAD SUCCESSFUL
    # ack clears CPadManager's current player, and the save payload's party
    # writes key off that player: without the ack-time re-selection the boot
    # ends with index -1 and every hero handle unresolved -- issue #83's
    # conversation-collision precondition, live-measured 2026-08-25.
    ack = runtime_source.find("void x2_override_004b1280(")
    reselect = runtime_source.find("x2_boot_player_select_primary(C,", ack)
    ack_call = runtime_source.find(retail_body_call(0x004B1280), ack)
    if min(ack, reselect, ack_call) < 0 or not ack < ack_call < reselect:
        refuse("the LOAD SUCCESSFUL ack does not re-select the primary "
               "player after the retail dialog body (the menu lifecycle "
               "clears CPadManager and the payload keys party writes off "
               "that player)")

    getter = player_source.find("base + PAD_MANAGER_RVA")
    setter = player_source.find("PAD_SET_CURRENT_PLAYER", getter)
    verify = player_source.find("PAD_CURRENT_PLAYER", setter)
    if min(getter, setter, verify) < 0 or not getter < setter < verify:
        refuse("boot player selection does not use and verify CPadManager's "
               "retail current-player setter")

    composition = startup_source.find("static int boot_to_host_mode(")
    direct = startup_source.find("x2_continue_boot_dispatch(C)", composition)
    menu = startup_source.find("x2_boot_menu_open(C, exe_base)", composition)
    if min(composition, direct, menu) < 0 or not composition < direct < menu:
        refuse("startup must dispatch the retail save chain directly at the "
               "intercepted intro command and keep x2_boot_menu_open as the "
               "refusal fallback")
    runtime_def = runtime_source.find("int x2_continue_boot_dispatch(")
    chain = runtime_source.find("catalog_for_show()", runtime_def)
    load = runtime_source.find("start_latest_load(C)", chain)
    pending = runtime_source.find("g_boot_load_pending = 1;", load)
    started = runtime_source.find(
        "x2_boot_mode_runtime_continue_started();", pending)
    if min(runtime_def, chain, load, pending, started) < 0 \
            or not runtime_def < chain < load < pending < started:
        refuse("the direct boot dispatch must catalog the cached leaf, arm "
               "start_latest_load, set the ack re-selection flag and only "
               "then consume the boot request")


def selftest():
    ordered = (retail_body_call(0x004B1280) + "\n"
               "x2_continue_transaction_take_success_ack(\n"
               "x86_guest_call_args(&call, g_exe + FN_SUCCESS_CALLBACK, 0u);")
    audit_success_ack_order(ordered)
    try:
        audit_success_ack_order("x2_continue_transaction_take_success_ack(\n"
                                "x86_guest_body(C, \"XMen2.exe\", 0x004b1280u)\n"
                                "x86_guest_call_args(&call, g_exe + "
                                "FN_SUCCESS_CALLBACK, 0u);")
    except WiringError:
        pass
    else:
        refuse("out-of-order success acknowledgement passed the audit")
    runtime = ("void x2_override_005c9260(struct CPU *C) {\n"
               "x2_boot_player_select_primary(C, PRIMARY_LOCAL_PLAYER);\n"
               "x86_guest_body(C, \"XMen2.exe\", 0x005c9260u)\n"
               "g_exe + FN_MAIN_MENU_HIDE;\n"
               "g_exe + FN_CONTINUE_CALLBACK;\n"
               "x2_boot_mode_runtime_continue_started();\n}\n"
               "void x2_override_004b1280(struct CPU *C) {\n"
               "x86_guest_body(C, \"XMen2.exe\", 0x004b1280u)\n"
               "x2_boot_player_select_primary(C, PRIMARY_LOCAL_PLAYER);\n}\n"
               "int x2_continue_boot_dispatch(struct CPU *C) {\n"
               "catalog_for_show();\n"
               "start_latest_load(C);\n"
               "g_boot_load_pending = 1;\n"
               "x2_boot_mode_runtime_continue_started();\n}")
    startup = ("static int boot_to_host_mode(struct CPU *C) {\n"
               "x2_continue_boot_dispatch(C);\n"
               "x2_boot_menu_open(C, exe_base);\n}")
    player = ("base + PAD_MANAGER_RVA;\n"
              "PAD_SET_CURRENT_PLAYER;\n"
              "PAD_CURRENT_PLAYER;\n")
    audit_boot_dispatch(runtime, startup, player)
    try:
        audit_boot_dispatch(runtime.replace(
            "x2_boot_player_select_primary(C, PRIMARY_LOCAL_PLAYER);\n", ""),
            startup, player)
    except WiringError:
        pass
    else:
        refuse("player-selection regression passed the Continue wiring audit")
    head, _, _ = runtime.partition("void x2_override_004b1280(")
    try:
        audit_boot_dispatch(head, startup, player)
    except WiringError:
        pass
    else:
        refuse("an ack without the player re-selection passed the audit")
    # The direct dispatch is what makes a Continue boot skip the splash,
    # the menu map and the menu.  Prove BOTH of its failure shapes are
    # caught: a startup that fell back to opening the menu unconditionally,
    # and a dispatch that consumed the boot request before the retail chain
    # accepted it (which loses the request when the manager refuses).
    try:
        audit_boot_dispatch(
            runtime, startup.replace("x2_continue_boot_dispatch(C);\n", ""),
            player)
    except WiringError:
        pass
    else:
        refuse("a startup that only opens the retail menu passed the audit")
    premature = runtime.replace(
        "catalog_for_show();\nstart_latest_load(C);\n"
        "g_boot_load_pending = 1;\n",
        "g_boot_load_pending = 1;\ncatalog_for_show();\n"
        "start_latest_load(C);\n")
    try:
        audit_boot_dispatch(premature, startup, player)
    except WiringError:
        pass
    else:
        refuse("an out-of-order direct dispatch passed the audit")
    print("continue_wiring --selftest: retained-first success acknowledgement "
          "and lifecycle-gated Continue accepted; out-of-order "
          "acknowledgement, missing player selection, a menu-only startup and "
          "an out-of-order direct dispatch rejected")
    return 0


def main():
    if sys.argv[1:] == ["--selftest"]:
        return selftest()
    if sys.argv[1:]:
        raise SystemExit("usage: check_continue_wiring.py [--selftest]")
    scanned_continue_entries()
    runtime_source = RUNTIME.read_text(encoding="utf-8")
    audit_boot_dispatch(runtime_source, STARTUP.read_text(encoding="utf-8"),
                        PLAYER.read_text(encoding="utf-8"))
    audit_success_ack_order(runtime_source)
    reached = audit_retail_bodies([RUNTIME, EXACT_LOAD], EXPECTED, "Continue")
    print(f"continue_wiring: {len(EXPECTED)}/{len(EXPECTED)} exact EPs "
          f"registered in their owning file; {reached} reach the retail body "
          "they wrap; the acknowledgement is retained-body first; boot "
          "Continue attempts the shared mode-3 chain before the retail menu "
          "fallback")
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except WiringError as error:
        raise SystemExit("continue_wiring: " + str(error)) from None
