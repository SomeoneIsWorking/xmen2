#!/usr/bin/env python3
"""Prove the Continue hooks enter the native-override routing boundary."""

from pathlib import Path
import sys

from check_save_trace_wiring import WiringError, audit_emitted_routes
from recomp_overrides import scan_overrides


ROOT = Path(__file__).resolve().parents[1]
RUNTIME = (ROOT / "src" / "native" / "continue_runtime.c").resolve()
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
    return [*bodies, "/* 00401000 CALL 0x0055ff00 */", call]


def selftest():
    current = synthetic_lines("DISPATCH(C, (G_IMGBASE + 0x15ff00U));")
    audit_emitted_routes([("synthetic-current.c", current)], EXPECTED,
                         "Continue")
    stale = synthetic_lines("fn_XMen2_0055ff00(C);")
    try:
        audit_emitted_routes([("synthetic-stale.c", stale)], EXPECTED,
                             "Continue")
    except WiringError as error:
        if ("bypassing the override slot" not in str(error)
                and "does not dispatch direct edge" not in str(error)):
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
    print("continue_wiring --selftest: current DISPATCH and retained-first "
          "success acknowledgement accepted; stale direct call and "
          "out-of-order acknowledgement rejected")
    return 0


def main():
    if sys.argv[1:] == ["--selftest"]:
        return selftest()
    if sys.argv[1:]:
        raise SystemExit("usage: check_continue_wiring.py [--selftest]")
    scanned_continue_entries()
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
    audit_success_ack_order(RUNTIME.read_text(encoding="utf-8"))
    print("continue_wiring: 4/4 exact EPs found; "
          f"{sum(calls.values())} direct edge(s) across "
          f"{sum(count > 0 for count in calls.values())}/4 EP(s) have direct "
          "edges (all route through DISPATCH); indirect entries use the "
          "authoritative runtime override table; 4/4 original bodies retained; "
          "dialog owner and retail success callback are plain RET and the "
          "acknowledgement is retained-body first")
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except WiringError as error:
        raise SystemExit("continue_wiring: " + str(error)) from None
