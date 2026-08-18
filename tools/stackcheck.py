#!/usr/bin/env python3
"""Check recorded guest call stack-deltas against what each callee's RET says.

The runtime records `<ep> <esp_in> <esp_out>` for every dispatched call while
X2_STACKCHECK is armed (see x86_stackcheck_arm). A correct guest call returns
with esp raised by 4 -- the return address the call pushed -- plus whatever the
callee's own `RET <imm>` pops on top. That expectation lives in the guest
binary, so the runtime records and this decides.

Why it exists: FUN_0046b750 stored its /GS cookie at entry_esp-4 while its
epilogue read [ESP+0x20], and those were FOUR BYTES apart. The /GS check then
compared a word that had never been the cookie and reported a stack buffer
overrun that had not happened. A single dword of drift inside a call tree
presents as memory corruption somewhere else entirely, so the drift itself has
to be checkable.

usage: stackcheck.py <record-file> <module.json> [<module.json> ...]
"""

import json
import sys


def ret_immediates(paths, skipped):
    """(module, ep) -> bytes the callee pops beyond the return address.

    Keyed on the module name AND the linked ep, because every libIG*.dll is
    linked for 0x10000000: a bare ep is a real function in eight of them.

    Refuses a module with no functions rather than contributing an empty map:
    an empty expectation table makes every delta 'unknown' and the whole run
    reads as clean.
    """
    out = {}
    for path in paths:
        with open(path) as f:
            data = json.load(f)
        functions = data.get("functions")
        if not functions:
            raise SystemExit(
                "stackcheck: %s has no functions -- refusing, because an empty "
                "expectation table reports every call as unknown and the run "
                "then looks clean." % path)
        program = data["program"]
        for fn in functions:
            lo = fn["ep"]
            hi = lo + fn.get("size", 0)
            # EVERY ret in the body, not the first one: a body whose rets
            # disagree cannot be given one expectation, and guessing from the
            # first is how a correct callee gets reported as an offender.
            imms = set()
            tail = False
            for ins in fn["ins"]:
                if ins["m"] == "RET":
                    text = ins["t"].split()
                    imms.add(int(text[1], 0) if len(text) > 1 else 0)
                elif ins["m"] == "JMP":
                    # A TAIL CALL leaves through someone else's RET, so this
                    # body's own RET does not describe its stack effect. Real
                    # case: FUN_0053f850 ends in `RET 0x4` on one path and
                    # `JMP dword ptr [EDX+0x30]` on the other, and scoring it
                    # by the RET reported 83 correct calls as -8 offenders.
                    flow = ins.get("flow")
                    if ins.get("ind") or flow is None or not lo <= flow < hi:
                        tail = True
            if tail:
                skipped.append((program, fn["ep"]))
            elif len(imms) == 1:
                out[(program, fn["ep"])] = imms.pop()
    return out


def main(argv):
    if len(argv) < 3:
        raise SystemExit(__doc__)
    record, modules = argv[1], argv[2:]
    tailcallers = []
    expected = ret_immediates(modules, tailcallers)
    print("stackcheck: %d function(s) have a known, single stack effect; %d "
          "excluded because they end in a TAIL CALL, whose pop belongs to the "
          "callee and cannot be read off this body."
          % (len(expected), len(tailcallers)))

    total = unknown = bad = 0
    offenders = {}
    with open(record) as f:
        lines = f.readlines()
    # The run this exists for ends in an abort, which cuts the last record
    # mid-write. That one truncated tail is expected and is reported by name;
    # a malformed line ANYWHERE else means the record is corrupt and is fatal,
    # because silently skipping records is how a checker reports a clean run
    # over data it could not read.
    truncated = 0
    if lines and len(lines[-1].split()) != 4:
        truncated = 1
        lines = lines[:-1]
        print("stackcheck: the final record is truncated -- the run aborted "
              "mid-write. Ignoring that one line only.")
    for line in lines:
            parts = line.split()
            if len(parts) != 4:
                raise SystemExit(
                    "stackcheck: %s has a malformed line %r that is NOT the "
                    "truncated tail. A record this tool cannot parse is a "
                    "failure, not something to skip." % (record, line))
            module = parts[0]
            ep, esp_in, esp_out = (int(p, 16) for p in parts[1:])
            total += 1
            key = (module, ep)
            if key not in expected:
                unknown += 1
                continue
            want = esp_in + 4 + expected[key]
            if esp_out != want:
                bad += 1
                k = (module, ep, esp_out - want)
                offenders[k] = offenders.get(k, 0) + 1

    print("stackcheck: %d dispatched call(s) recorded (%d truncated), %d "
          "checked, %d with no known RET (not checked)"
          % (total, truncated, total - unknown, unknown))
    if not total:
        raise SystemExit(
            "stackcheck: the record is EMPTY. Nothing was checked -- this is "
            "not a pass.")
    if not offenders:
        print("stackcheck: every checked call returned the esp its RET "
              "promises. The imbalance is NOT at a dispatched call boundary "
              "-- it is inside a body, or at a DIRECT call, which this cannot "
              "see.")
        return 0
    print("stackcheck: %d call(s) returned an esp their RET does not explain:"
          % bad)
    for (module, ep, drift), n in sorted(offenders.items(),
                                         key=lambda kv: -kv[1]):
        print("  %-16s 0x%08x  drift %+d byte(s)  x%d"
              % (module, ep, drift, n))
    return 1


def _selftest():
    """Show the checker giving BOTH answers on data whose right answer is known.

    A checker that has only ever reported "everything is fine" is not known to
    be able to report anything else, and this one reports a clean run over a
    million calls -- exactly the shape of a check that never fires. So: a call
    that returns the esp its RET promises must PASS, a call four bytes out must
    be FLAGGED with that drift, and a body with a tail call must be excluded
    rather than scored against its own RET.
    """
    import os
    import tempfile

    module = {
        "program": "Test.exe",
        "functions": [
            # RET 0x8: pops the return address plus 8.
            {"ep": 0x1000, "size": 4,
             "ins": [{"a": 0x1000, "m": "RET", "t": "RET 0x8"}]},
            # Ends in a tail JMP as well as a RET: no single expectation.
            {"ep": 0x2000, "size": 8,
             "ins": [{"a": 0x2000, "m": "JMP", "t": "JMP dword ptr [EAX]",
                      "ind": 1},
                     {"a": 0x2004, "m": "RET", "t": "RET 0x4"}]},
        ],
    }
    d = tempfile.mkdtemp(prefix="scsel")
    jpath = os.path.join(d, "Test.json")
    with open(jpath, "w") as f:
        json.dump(module, f)

    def run(records):
        rpath = os.path.join(d, "rec.txt")
        with open(rpath, "w") as f:
            f.write("".join(records))
        return main(["stackcheck.py", rpath, jpath])

    fails = 0
    # A correct call: esp_in 1000 -> 1000 + 4 + 8 = 100c.
    if run(["Test.exe 00001000 00001000 0000100c\n"]) != 0:
        print("  FAIL  a call matching its RET was reported as an offender")
        fails += 1
    else:
        print("  ok    a call matching its RET passes")
    # Four bytes short -- the drift this tool was built to find.
    if run(["Test.exe 00001000 00001000 00001008\n"]) == 0:
        print("  FAIL  a call 4 bytes out of balance was NOT flagged")
        fails += 1
    else:
        print("  ok    a call 4 bytes out of balance is flagged")
    # The tail-caller must be excluded, not scored against its own RET.
    if run(["Test.exe 00002000 00001000 00001008\n"]) != 0:
        print("  FAIL  a tail-calling body was scored against its own RET")
        fails += 1
    else:
        print("  ok    a tail-calling body is excluded, not scored")

    print("stackcheck --selftest: %s (%d of 3 case(s) failed)"
          % ("FAILED" if fails else "PASSED", fails))
    return fails


if __name__ == "__main__":
    if sys.argv[1:] == ["--selftest"]:
        sys.exit(_selftest())
    sys.exit(main(sys.argv))
