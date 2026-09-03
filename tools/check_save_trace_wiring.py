#!/usr/bin/env python3
"""Prove every save-trace seam enters the native-override routing boundary."""

from pathlib import Path
import sys
import tempfile

from native_overrides import scan_overrides


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


def retail_body_call(ep, cpu="C", module="XMen2.exe"):
    """How an override runs the function it replaced, since the emitter went:
    by asking the dispatcher for the retail body at a MAPPED address. `cpu` is
    the CPU pointer's name in the calling source, which differs by file."""
    return f'x86_guest_body({cpu}, "{module}", 0x{ep:08x}u)'


def audit_retail_bodies(paths=None, expected=EXPECTED, feature="traced"):
    """Every audited EP's override must still reach the retail body.

    An override that stops calling the body it wrapped does not fail -- it
    silently replaces the function, which is the one failure mode of this
    whole mechanism that nothing else notices."""
    if paths is None:
        paths = sorted({EXPECTED_OWNER[ep] for ep in expected})
    text = "\n".join(Path(path).read_text(encoding="utf-8") for path in paths)
    missing = [ep for ep in sorted(expected) if retail_body_call(ep) not in text]
    if missing:
        refuse(f"{len(missing)} of {len(expected)} {feature} override(s) never "
               "reach the retail body they wrap: "
               + ", ".join(f"0x{ep:08x}" for ep in missing))
    return len(expected)


def selftest():
    scratch = ROOT / "scratch"
    scratch.mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="save-trace-wiring-",
                                     dir=scratch) as directory:
        tree = Path(directory)
        (tree / "src" / "native").mkdir(parents=True)
        registration = ('x86_register_override("XMen2.exe", '
                        '0x005604f0, trace);\n')
        body = retail_body_call(0x005604F0) + ";\n"
        source = tree / "src" / "native" / "trace.c"
        source.write_text(registration + body)
        if [o.ep for o in scan_overrides(tree)] != [0x005604F0]:
            refuse("the scanner did not read a plain registration")
        audit_retail_bodies([source], {0x005604F0})
        source.write_text(registration)
        try:
            audit_retail_bodies([source], {0x005604F0})
        except WiringError:
            pass
        else:
            refuse("an override that never reaches the retail body passed")
    print("save_trace_wiring --selftest: a registration is read, an override "
          "that reaches its retail body is accepted, and one that does not is "
          "rejected")
    return 0


def main():
    if sys.argv[1:] == ["--selftest"]:
        return selftest()
    if sys.argv[1:]:
        raise SystemExit("usage: check_save_trace_wiring.py [--selftest]")
    scanned = scanned_save_trace_entries()
    reached = audit_retail_bodies()
    print(f"save_trace_wiring: {len(scanned)}/{len(EXPECTED)} exact EPs "
          f"registered in their owning file; {reached}/{len(EXPECTED)} reach "
          "the retail body they wrap")
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except WiringError as error:
        raise SystemExit("save_trace_wiring: " + str(error)) from None
