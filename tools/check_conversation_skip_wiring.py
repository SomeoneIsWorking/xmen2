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
    if early.count("conversation_cutscene_skip_observe_inactive(") != 2 \
            or early.count("call0(C, FN_INPUT, 0));") != 2:
        raise WiringError("disabled and invisible update exits must both "
                          "observe inactive skip state with the live input "
                          "manager, so an Escape during a camera-only "
                          "stretch arms the latch")
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
    # The wait floor: the scheduler-insert override shortens scripted waits
    # ONLY while the latch holds and never below the floor. The floor is the
    # fix for the 82bdf13 regression: a zero floor let the next
    # startConversation race the conversation manager's ending unwind and
    # reproduced the issue #83 no-line signature.
    require(runtime, "x2_override_004d6a00", "scheduler wait clamp")
    require(runtime, "FN_SCHEDULE_WAIT    0x004d6a00u", "scheduler wait clamp")
    require(runtime, "WAIT_FLOOR_S", "wait floor constant")
    require(runtime, "wait_scope_allows(", "wait owner scoping")
    require(runtime, "wait_deadline_clamped(", "wait floor clamp")
    require(runtime, "script waits %lu/%lu floor-limited ",
            "wait clamp diagnostic")
    if "004d9130" in runtime:
        raise WiringError("conversation skip must not override retail waittimed")
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
    discriminators = (
        (0, "conversation_cutscene_skip_observe_inactive("),
        (1, "conversation_skip_policy_is_authored("),
        (1, "CUTSCENE_SKIP_ACTION 20u"),
        (1, "x2_override_004d6a00"),
        (1, "WAIT_FLOOR_S"),
        (1, "wait_scope_allows("),
        (1, "wait_deadline_clamped("),
        (1, "script waits %lu/%lu floor-limited "),
        (0, "call1(C, vslot(self, 0x18u), self,"),
        (2, "conversation_cutscene_skip_probe("),
        (3, "src/native/conversation_cutscene_skip.c"),
    )
    for index, needle in discriminators:
        broken = list(current)
        # A source can appear in both its focused test and x2native. Remove
        # every occurrence so this falsifier still proves the shipping source
        # list is required instead of accidentally deleting only the test TU.
        broken[index] = broken[index].replace(needle, "")
        try:
            audit(*broken)
        except WiringError:
            continue
        raise WiringError(f"negative discriminator passed after removing {needle!r}")
    broken = list(current)
    broken[1] += '\nx86_register_override("XMen2.exe", 0x004d9130u, fn);\n'
    try:
        audit(*broken)
    except WiringError:
        pass
    else:
        raise WiringError("negative discriminator admitted a waittimed override")
    print(f"conversation_skip_wiring --selftest: {1 + len(discriminators)} broken chains rejected")


def main():
    if sys.argv[1:] == ["--selftest"]:
        selftest()
    elif sys.argv[1:]:
        raise SystemExit("usage: check_conversation_skip_wiring.py [--selftest]")
    else:
        audit(*production_sources())
        print("conversation_skip_wiring: action20 -> authored policy -> retail "
              "chooseResponse; script waits floor-limited only while the "
              "latch holds, waittimed itself unmodified")


if __name__ == "__main__":
    try:
        main()
    except WiringError as error:
        raise SystemExit(f"conversation_skip_wiring: {error}") from None
