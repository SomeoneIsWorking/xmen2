#!/usr/bin/env python3
"""Prove every save-trace seam enters the native-override routing boundary."""

import re
from pathlib import Path
import tempfile
import sys

from recomp_overrides import scan_overrides


ROOT = Path(__file__).resolve().parents[1]
TRACE_RUNTIME = ROOT / "src" / "native" / "save_trace_runtime.c"
CONTINUE_RUNTIME = ROOT / "src" / "native" / "continue_runtime.c"
AUTOSAVE_RUNTIME = ROOT / "src" / "native" / "autosave_runtime.c"
EXPECTED = {
    0x005C9970,
    0x005C9260,
    0x0055FCD0,
    0x004AED10,
    0x0046E2B0,
    0x0049F140,
    0x004AEB80,
    0x004AE990,
    0x004B15B0,
    0x0046BAF0,
    0x0055FE70,
    0x00484CE0,
    0x0049F860,
    0x004A6B50,
    0x005604F0,
}
EXPECTED_OWNER = {
    ep: (CONTINUE_RUNTIME if ep == 0x005C9260 else
         AUTOSAVE_RUNTIME if ep == 0x00484CE0 else TRACE_RUNTIME)
    for ep in EXPECTED
}
CALL = re.compile(r"/\* [0-9a-f]{8} (?:CALL|JMP) 0x([0-9a-f]{8}) \*/")
BODY = re.compile(r"/\* FUN_([0-9a-f]{8})  @ 0x[0-9a-f]{8}")
DIRECT = re.compile(r"\bfn_XMen2_([0-9a-f]{8})\(C\);")


class WiringError(Exception):
    pass


def refuse(message):
    raise WiringError(message)


def scanned_save_trace_entries():
    found = {}
    for module, ep, path, _ in scan_overrides(ROOT):
        resolved = Path(path).resolve()
        if module == "XMen2.exe" and resolved in {
                TRACE_RUNTIME, CONTINUE_RUNTIME,
                AUTOSAVE_RUNTIME} and ep in EXPECTED:
            if ep in found:
                refuse(f"duplicate registration for 0x{ep:08x}")
            found[ep] = resolved
    if set(found) != EXPECTED:
        found_entries = set(found)
        missing = ", ".join(
            f"0x{ep:08x}" for ep in sorted(EXPECTED - found_entries))
        extra = ", ".join(
            f"0x{ep:08x}" for ep in sorted(found_entries - EXPECTED))
        refuse(f"authoritative src/native scan mismatch; missing [{missing}], "
               f"extra [{extra}]")
    wrong = [ep for ep, path in found.items() if path != EXPECTED_OWNER[ep]]
    if wrong:
        details = ", ".join(
            f"0x{ep:08x} in {found[ep].name}, expected "
            f"{EXPECTED_OWNER[ep].name}" for ep in sorted(wrong))
        refuse(f"registration owner mismatch: {details}")
    return set(found)


def wrapper_body_lines(lines):
    """Line indexes inside fn_XMen2_<ep>_e definitions. The wrapper's own
    fallback tail calls the raw body -- that is the routing boundary itself,
    not a bypass of it."""
    inside = set()
    for index, line in enumerate(lines):
        if re.match(r"void fn_XMen2_[0-9a-f]{8}_e\(CPU \*C\) \{", line):
            inside.update(range(index, index + 5))
    return inside


def audit_emitted_routes(chunks, expected=EXPECTED, feature="traced",
                         require_direct=True):
    calls = {ep: 0 for ep in expected}
    retained = set()

    for label, lines in chunks:
        wrappers = wrapper_body_lines(lines)
        for index, line in enumerate(lines):
            body = BODY.search(line)
            if body and int(body.group(1), 16) in expected:
                retained.add(int(body.group(1), 16))
            direct = DIRECT.search(line)
            if direct and index not in wrappers \
                    and int(direct.group(1), 16) in expected:
                refuse(f"{label}:{index + 1} directly calls {feature} EP "
                       f"0x{int(direct.group(1), 16):08x}, bypassing the "
                       "override slot")
            match = CALL.search(line)
            if not match:
                continue
            ep = int(match.group(1), 16)
            if ep not in expected:
                continue
            calls[ep] += 1
            window = "\n".join(lines[index + 1:index + 5])
            expected_call = f"fn_XMen2_{ep:08x}_e(C);"
            if expected_call not in window:
                refuse(f"{label}:{index + 1} does not route its call to "
                       f"0x{ep:08x} through the runtime override wrapper")

    if retained != expected:
        missing = ", ".join(
            f"0x{ep:08x}" for ep in sorted(expected - retained))
        refuse(f"generated XMen2 output dropped retained body/bodies [{missing}]")
    # The wrapper is only half of the routing boundary; the other half is the
    # runtime slot it consults. recomp-x86 5a241a6 took overrides out of the
    # emitter: the emitted CALL goes through fn_XMen2_<ep>_e, which reads
    # _ov_<ep> at run time -- so the slot must exist for every audited EP.
    text = "\n".join("\n".join(lines) for _, lines in chunks)
    missing_slots = [ep for ep in sorted(expected)
                     if f"static x86_override_fn _ov_{ep:08x};" not in text]
    if missing_slots:
        refuse("generated output lacks runtime override slot(s) "
               + ", ".join(f"0x{ep:08x}" for ep in missing_slots))
    routed = sum(calls.values())
    if require_direct and not routed:
        refuse("generated output contains none of the traced direct call edges")
    return calls


def verify_emitted_routes(paths):
    chunks = [
        (str(path), path.read_text(encoding="utf-8").splitlines())
        for path in paths
    ]
    calls = audit_emitted_routes(chunks)
    routed = sum(calls.values())
    print("save_trace_wiring: 15/15 exact EPs found by the authoritative "
          f"src/native scan; {routed} direct edge(s) across "
          f"{sum(count > 0 for count in calls.values())}/15 EP(s) all route "
          "through the runtime override wrapper; 15/15 original bodies "
          "retained")


def synthetic_lines(call):
    bodies = [f"/* FUN_{ep:08x}  @ 0x{ep:08x}  (1 instrs) */"
              for ep in sorted(EXPECTED)]
    wrappers = [f"void fn_XMen2_{ep:08x}_e(CPU *C) {{ /* stub */ }}"
                for ep in sorted(EXPECTED)]
    slots = [f"static x86_override_fn _ov_{ep:08x};"
             for ep in sorted(EXPECTED)]
    return [*bodies, *wrappers, *slots,
            "/* 00401000 CALL 0x005604f0 */", call]


def selftest():
    scratch = ROOT / "scratch"
    scratch.mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="save-trace-wiring-",
                                     dir=scratch) as directory:
        tree = Path(directory)
        (tree / "src" / "native").mkdir(parents=True)
        (tree / "src" / "save").mkdir()
        registration = ('x86_register_override("XMen2.exe", '
                        '0x005604f0, trace);\n')
        (tree / "src" / "save" / "trace.c").write_text(registration)
        if scan_overrides(tree):
            refuse("scanner claimed a registration outside src/native")
        (tree / "src" / "native" / "trace.c").write_text(registration)
        if len(scan_overrides(tree)) != 1:
            refuse("scanner did not claim the same registration in src/native")

    current = synthetic_lines("fn_XMen2_005604f0_e(C);")
    audit_emitted_routes([("synthetic-current.c", current)])
    stale = synthetic_lines("fn_XMen2_005604f0(C);")
    try:
        audit_emitted_routes([("synthetic-stale.c", stale)])
    except WiringError as error:
        if ("bypassing the override slot" not in str(error)
                and "does not route its call to" not in str(error)):
            refuse(f"stale discriminator refused for the wrong reason: {error}")
    else:
        refuse("stale direct call passed the routing audit")
    print("save_trace_wiring --selftest: outside-scan registration ignored, "
          "inside-scan registration accepted, current override-wrapper call "
          "accepted, stale direct call rejected")
    return 0


def main():
    if sys.argv[1:] == ["--selftest"]:
        return selftest()
    if sys.argv[1:]:
        raise SystemExit("usage: check_save_trace_wiring.py [--selftest]")
    scanned_save_trace_entries()
    chunks = sorted((ROOT / "src" / "recomp").glob("XMen2_[0-9][0-9][0-9].c"))
    if not chunks:
        print("save_trace_wiring: 15/15 exact EPs found by the authoritative "
              "src/native scan; generated XMen2 output is absent, so direct "
              "routing verification is SKIPPED")
        return 77
    verify_emitted_routes(chunks)
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except WiringError as error:
        raise SystemExit("save_trace_wiring: " + str(error)) from None
