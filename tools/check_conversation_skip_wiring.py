#!/usr/bin/env python3
"""Pin action 20 to the cleanup-preserving conversation advance chain."""

from pathlib import Path
import sys


ROOT = Path(__file__).resolve().parents[1]


class WiringError(Exception):
    pass


def require(source, needle, where):
    if needle not in source:
        raise WiringError(f"{where} is missing {needle!r}")


def audit(conversation, runtime, probe, cmake):
    update_begin = conversation.find("void x2_override_0045d1a0")
    begin = conversation.find("Accept advances once")
    end = conversation.find("0x0045d3bf", begin)
    if update_begin < 0 or begin < 0 or end < 0:
        raise WiringError("conversation advance block could not be bounded")
    early = conversation[update_begin:begin]
    block = conversation[begin:end]
    if early.count("conversation_cutscene_skip_observe_inactive(self);") != 2:
        raise WiringError("disabled and invisible update exits must both "
                          "observe inactive skip state")
    require(block, "conversation_cutscene_skip_should_advance(",
            "conversation action gate")
    require(block, "if (advance)", "conversation action gate")
    require(block, "call1(C, vslot(self, 0x18u), self,",
            "retail chooseResponse dispatch")
    require(runtime, "conversation_skip_policy_update(", "runtime policy")
    require(runtime, "conversation_skip_policy_is_authored(",
            "runtime authored classifier")
    require(runtime, "CUTSCENE_SKIP_ACTION 20u", "runtime action gate")
    require(runtime, "INPUT_ACTION_MASK", "runtime action gate")
    require(runtime, "CONVERSATION_SKIP_RESPONSE_DETERMINISTIC",
            "runtime response classifier")
    require(probe, "conversation_cutscene_skip_probe(", "live probe")
    require(cmake, "src/native/conversation_skip_policy.c", "x2native sources")
    require(cmake, "src/native/conversation_cutscene_skip.c", "x2native sources")


def production_sources():
    return (
        (ROOT / "src/native/conversation.c").read_text(),
        (ROOT / "src/native/conversation_cutscene_skip.c").read_text(),
        (ROOT / "src/native/cutscene_skip_probe.c").read_text(),
        (ROOT / "CMakeLists.txt").read_text(),
    )


def selftest():
    current = production_sources()
    audit(*current)
    for index, needle in (
        (0, "conversation_cutscene_skip_observe_inactive(self);"),
        (1, "conversation_skip_policy_is_authored("),
        (1, "CUTSCENE_SKIP_ACTION 20u"),
        (0, "call1(C, vslot(self, 0x18u), self,"),
        (2, "conversation_cutscene_skip_probe("),
        (3, "src/native/conversation_cutscene_skip.c"),
    ):
        broken = list(current)
        broken[index] = broken[index].replace(needle, "", 1)
        try:
            audit(*broken)
        except WiringError:
            continue
        raise WiringError(f"negative discriminator passed after removing {needle!r}")
    print("conversation_skip_wiring --selftest: 6/6 broken chains rejected")


def main():
    if sys.argv[1:] == ["--selftest"]:
        selftest()
    elif sys.argv[1:]:
        raise SystemExit("usage: check_conversation_skip_wiring.py [--selftest]")
    else:
        audit(*production_sources())
        print("conversation_skip_wiring: action20 -> authored policy -> retail "
              "chooseResponse, production probe and x2native sources verified")


if __name__ == "__main__":
    try:
        main()
    except WiringError as error:
        raise SystemExit(f"conversation_skip_wiring: {error}") from None
