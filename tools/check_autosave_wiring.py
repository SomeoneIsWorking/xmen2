#!/usr/bin/env python3
"""Pin production autosave ownership independently of optional save tracing."""

from pathlib import Path
import sys

from check_save_trace_wiring import retail_body_call
from native_overrides import scan_overrides


ROOT = Path(__file__).resolve().parents[1]
RUNTIME = (ROOT / "src" / "native" / "autosave_runtime.c").resolve()


class WiringError(Exception):
    pass


def require_order(text, names):
    positions = [text.find(name) for name in names]
    if min(positions) < 0 or positions != sorted(positions):
        raise WiringError("required production order missing: "
                          + " -> ".join(names))


def audit(runtime, menu, control):
    registrations = {
        ep for module, ep, path, _ in scan_overrides(ROOT)
        if module == "XMen2.exe" and Path(path).resolve() == RUNTIME
    }
    if registrations != {0x00484CE0}:
        raise WiringError("autosave owner must register exactly 0x00484ce0; "
                          f"found {sorted(registrations)}")
    if "X2_SAVE_TRACE" in runtime:
        raise WiringError("production autosave registration is gated by the "
                          "optional save trace")
    require_order(runtime, [
        retail_body_call(0x00484CE0),
        "x2_save_trace_map_return(map, succeeded);",
        "x2_autosave_runtime_map_return(succeeded);",
    ])
    require_order(runtime, [
        "serialize_snapshot(source)",
        "x2_autosave_header_from_payload(",
        "x2_retail_save_directory()",
        "x2_autosave_storage_publish(",
    ])
    if "x2_autosave_runtime_menu_show();" not in menu:
        raise WiringError("CMenuMain::Show does not cancel the menu-map "
                          "checkpoint")
    if "x2_autosave_runtime_poll(cpu);" not in control:
        raise WiringError("guest input poll does not drive autosave policy")


def selftest():
    valid = "a(); b(); c();"
    require_order(valid, ["a();", "b();", "c();"])
    try:
        require_order(valid, ["b();", "a();", "c();"])
    except WiringError:
        pass
    else:
        raise WiringError("out-of-order chain passed")
    print("autosave_wiring --selftest: ordered chain accepted; out-of-order "
          "chain rejected")
    return 0


def main():
    if sys.argv[1:] == ["--selftest"]:
        return selftest()
    if sys.argv[1:]:
        raise SystemExit("usage: check_autosave_wiring.py [--selftest]")
    audit(RUNTIME.read_text(encoding="utf-8"),
          (ROOT / "src/native/continue_runtime.c").read_text(encoding="utf-8"),
          (ROOT / "src/native/control.c").read_text(encoding="utf-8"))
    print("autosave_wiring: unconditional 0x00484ce0 owner retained-first; "
          "trace marker + MAP_LOAD schedule ordered; menu cancellation and "
          "guest-poll driver linked; serialize -> header -> directory -> "
          "transactional publish ordered")
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except WiringError as error:
        raise SystemExit("autosave_wiring: " + str(error)) from None
