#!/usr/bin/env python3
"""Prove the Continue hooks enter the native-override routing boundary."""

from pathlib import Path
import sys

from check_save_trace_wiring import WiringError, audit_emitted_routes
from recomp_overrides import scan_overrides


ROOT = Path(__file__).resolve().parents[1]
RUNTIME = (ROOT / "src" / "native" / "continue_runtime.c").resolve()
STARTUP = (ROOT / "src" / "native" / "startup.c").resolve()
PLAYER = (ROOT / "src" / "native" / "boot_player_selection.c").resolve()
EXPECTED = {0x005C9260, 0x005F2B70, 0x0055FF00, 0x004B1280}
PLAIN_RET = {0x004B1280, 0x0049F140}


def refuse(message):
    raise WiringError(message)


def scanned_continue_entries():
    found = {
        ep
        for module, ep, path, _ in scan_overrides(ROOT)
        if module == "XMen2.exe" and Path(path).resolve() == RUNTIME
    }
    if found != EXPECTED:
        missing = ", ".join(f"0x{ep:08x}" for ep in sorted(EXPECTED - found))
        extra = ", ".join(f"0x{ep:08x}" for ep in sorted(found - EXPECTED))
        refuse(f"authoritative src/native scan mismatch; missing [{missing}], "
               f"extra [{extra}]")


def audit_success_ack_order(source):
    retained = source.find("fn_XMen2_004b1280(C);")
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
    show = runtime_source.find("fn_XMen2_005c9260(C);", dispatch)
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
    ack_call = runtime_source.find("fn_XMen2_004b1280(C);", ack)
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


def audit_plain_ret_abis(chunks):
    bodies = {}
    for label, lines in chunks:
        starts = []
        for index, line in enumerate(lines):
            if line.startswith("/* FUN_"):
                starts.append((index, int(line[7:15], 16)))
        for position, (index, ep) in enumerate(starts):
            if ep not in PLAIN_RET:
                continue
            end = starts[position + 1][0] if position + 1 < len(starts) \
                else len(lines)
            bodies[ep] = (label, lines[index:end])
    missing = PLAIN_RET - set(bodies)
    if missing:
        refuse("generated output lacks ABI body/bodies "
               + ", ".join(f"0x{ep:08x}" for ep in sorted(missing)))
    for ep, (label, lines) in bodies.items():
        returns = [line for line in lines if " RET" in line and "/*" in line]
        if not returns or any("RET 0x" in line for line in returns):
            refuse(f"{label}: 0x{ep:08x} is not a plain-RET ABI")


def synthetic_lines(call):
    bodies = [f"/* FUN_{ep:08x}  @ 0x{ep:08x}  (1 instrs) */"
              for ep in sorted(EXPECTED | PLAIN_RET)]
    slots = [f"static x86_override_fn _ov_{ep:08x};"
             for ep in sorted(EXPECTED)]
    wrappers = [f"void fn_XMen2_{ep:08x}_e(CPU *C) {{ /* stub */ }}"
                for ep in sorted(EXPECTED)]
    return [*bodies, *wrappers, *slots,
            "/* 00401000 CALL 0x0055ff00 */", call]


def selftest():
    current = synthetic_lines("fn_XMen2_0055ff00_e(C);")
    audit_emitted_routes([("synthetic-current.c", current)], EXPECTED,
                         "Continue")
    stale = synthetic_lines("fn_XMen2_0055ff00(C);")
    try:
        audit_emitted_routes([("synthetic-stale.c", stale)], EXPECTED,
                             "Continue")
    except WiringError as error:
        if ("bypassing the override slot" not in str(error)
                and "does not route its call to" not in str(error)):
            refuse(f"stale discriminator refused for the wrong reason: {error}")
    else:
        refuse("stale direct call passed the Continue routing audit")
    ordered = ("fn_XMen2_004b1280(C);\n"
               "x2_continue_transaction_take_success_ack(\n"
               "x86_guest_call_args(&call, g_exe + FN_SUCCESS_CALLBACK, 0u);")
    audit_success_ack_order(ordered)
    try:
        audit_success_ack_order("x2_continue_transaction_take_success_ack(\n"
                                "fn_XMen2_004b1280(C);\n"
                                "x86_guest_call_args(&call, g_exe + "
                                "FN_SUCCESS_CALLBACK, 0u);")
    except WiringError:
        pass
    else:
        refuse("out-of-order success acknowledgement passed the audit")
    runtime = ("void x2_override_005c9260(struct CPU *C) {\n"
               "x2_boot_player_select_primary(C, PRIMARY_LOCAL_PLAYER);\n"
               "fn_XMen2_005c9260(C);\n"
               "g_exe + FN_MAIN_MENU_HIDE;\n"
               "g_exe + FN_CONTINUE_CALLBACK;\n"
               "x2_boot_mode_runtime_continue_started();\n}\n"
               "void x2_override_004b1280(struct CPU *C) {\n"
               "fn_XMen2_004b1280(C);\n"
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
    print("continue_wiring --selftest: current DISPATCH, retained-first "
          "success acknowledgement and lifecycle-gated Continue accepted; "
          "stale direct call, out-of-order acknowledgement, missing player "
          "selection, a menu-only startup and an out-of-order direct "
          "dispatch rejected")
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
    chunks = sorted((ROOT / "src" / "recomp").glob(
        "XMen2_[0-9][0-9][0-9].c"))
    if not chunks:
        print("continue_wiring: 4/4 exact EPs found by the authoritative "
              "src/native scan; generated XMen2 output is absent, so direct "
              "routing verification is SKIPPED")
        return 77
    sources = [
        (str(path), path.read_text(encoding="utf-8").splitlines())
        for path in chunks
    ]
    calls = audit_emitted_routes(sources, EXPECTED, "Continue",
                                 require_direct=False)
    audit_plain_ret_abis(sources)
    audit_success_ack_order(runtime_source)
    print("continue_wiring: 4/4 exact EPs found; "
          f"{sum(calls.values())} direct edge(s) across "
          f"{sum(count > 0 for count in calls.values())}/4 EP(s) have direct "
          "edges (all route through DISPATCH); indirect entries use the "
          "authoritative runtime override table; 4/4 original bodies retained; "
          "dialog owner and retail success callback are plain RET and the "
          "acknowledgement is retained-body first; boot Continue attempts "
          "the shared mode-3 chain before the retail menu fallback")
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except WiringError as error:
        raise SystemExit("continue_wiring: " + str(error)) from None
