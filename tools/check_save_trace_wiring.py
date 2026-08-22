#!/usr/bin/env python3
"""Prove every save-trace seam enters the native-override routing boundary."""

import re
from pathlib import Path
import tempfile
import sys

from recomp_overrides import scan_overrides


ROOT = Path(__file__).resolve().parents[1]
RUNTIME = ROOT / "src" / "native" / "save_trace_runtime.c"
EXPECTED = {
    0x005C9970,
    0x005C9260,
    0x0055FCD0,
    0x004AED10,
    0x0046E2B0,
    0x0049F150,
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
CALL = re.compile(r"/\* [0-9a-f]{8} (?:CALL|JMP) 0x([0-9a-f]{8}) \*/")
BODY = re.compile(r"/\* FUN_([0-9a-f]{8})  @ 0x[0-9a-f]{8}")
DIRECT = re.compile(r"\bfn_XMen2_([0-9a-f]{8})\(C\);")


class WiringError(Exception):
    pass


def refuse(message):
    raise WiringError(message)


def scanned_save_trace_entries():
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
    return found


def audit_emitted_routes(chunks):
    calls = {ep: 0 for ep in EXPECTED}
    retained = set()

    for label, lines in chunks:
        for index, line in enumerate(lines):
            body = BODY.search(line)
            if body and int(body.group(1), 16) in EXPECTED:
                retained.add(int(body.group(1), 16))
            direct = DIRECT.search(line)
            if direct and int(direct.group(1), 16) in EXPECTED:
                refuse(f"{label}:{index + 1} directly calls traced EP "
                       f"0x{int(direct.group(1), 16):08x}, bypassing the "
                       "override slot")
            match = CALL.search(line)
            if not match:
                continue
            ep = int(match.group(1), 16)
            if ep not in EXPECTED:
                continue
            calls[ep] += 1
            window = "\n".join(lines[index + 1:index + 5])
            offset = ep - 0x00400000
            expected_dispatch = f"DISPATCH(C, (G_IMGBASE + 0x{offset:x}U));"
            if expected_dispatch not in window:
                refuse(f"{label}:{index + 1} does not dispatch direct edge to "
                       f"0x{ep:08x} through the override slot")

    if retained != EXPECTED:
        missing = ", ".join(f"0x{ep:08x}" for ep in sorted(EXPECTED - retained))
        refuse(f"generated XMen2 output dropped retained body/bodies [{missing}]")
    routed = sum(calls.values())
    if not routed:
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
          "through DISPATCH; 15/15 original bodies retained")


def synthetic_lines(call):
    bodies = [f"/* FUN_{ep:08x}  @ 0x{ep:08x}  (1 instrs) */"
              for ep in sorted(EXPECTED)]
    return [*bodies, "/* 00401000 CALL 0x005604f0 */", call]


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

    current = synthetic_lines("DISPATCH(C, (G_IMGBASE + 0x1604f0U));")
    audit_emitted_routes([("synthetic-current.c", current)])
    stale = synthetic_lines("fn_XMen2_005604f0(C);")
    try:
        audit_emitted_routes([("synthetic-stale.c", stale)])
    except WiringError as error:
        if ("bypassing the override slot" not in str(error)
                and "does not dispatch direct edge" not in str(error)):
            refuse(f"stale discriminator refused for the wrong reason: {error}")
    else:
        refuse("stale direct call passed the routing audit")
    print("save_trace_wiring --selftest: outside-scan registration ignored, "
          "inside-scan registration accepted, current DISPATCH accepted, "
          "stale direct call rejected")
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
