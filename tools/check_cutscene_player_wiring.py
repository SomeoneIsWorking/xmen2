#!/usr/bin/env python3
"""Pin action 20 to the cutscene player, not a conversation payload wait."""

from pathlib import Path
import sys


ROOT = Path(__file__).resolve().parents[1]


class WiringError(Exception):
    pass


def require(source, needle, where):
    if needle not in source:
        raise WiringError(f"{where} is missing {needle!r}")


def reject(source, needle, where):
    if needle in source:
        raise WiringError(f"{where} still contains forbidden {needle!r}")


def audit(conversation, player, context, behaved, event, dialogue,
          script_audio, dsound, probe, cmake):
    update_begin = conversation.find("void x2_override_0045d1a0")
    begin = conversation.find("Accept advances once", update_begin)
    end = conversation.find("0x0045d3bf", begin)
    if update_begin < 0 or begin < 0 or end < 0:
        raise WiringError("conversation advance block could not be bounded")
    update = conversation[update_begin:end]
    require(update, "uint32_t action = 4u", "retail conversation input")
    require(update, "call1(C, vslot(self, 0x18u), self,",
            "retail chooseResponse dispatch")
    reject(update, "action = 20u", "conversation payload")
    reject(update, "cutscene_player", "conversation payload")
    reject(conversation, "action = 20u", "conversation subsystem")

    require(player, "x2_override_004a00d0", "cutscene player update owner")
    require(player, "CINEMATIC_SKIP_ACTION   20u", "cutscene input edge")
    require(player, "x2_cutscene_player_finish(", "cutscene policy")
    require(player, "behaved_player_next_owned(", "owned-fiber selection")
    require(player, "behaved_player_step_context(", "owned-fiber execution")
    require(player, "cutscene_event_player_next_owned(",
            "owned-event selection")
    require(player, "cutscene_event_player_step_owned_slot(",
            "owned-event execution")
    require(player, "cutscene_dialogue_advance(",
            "cutscene-owned silent dialogue payload")
    require(player, "cutscene_dialogue_skip_begin(",
            "whole-player dialogue suppression scope")
    require(player, "cutscene_dialogue_skip_end(",
            "whole-player dialogue suppression scope")
    require(player, "cutscene_player_silences_current_context(",
            "cutscene-owned audio predicate")
    require(player, "CLOCK_CONTROL_DEADLINE", "authored control release")
    require(player, "same_guest_time", "one-step clock invariant")
    require(player, "same_frame", "one-step frame invariant")

    require(context, "x2_override_004d8b30", "retail BehavEd context runner")
    require(context, "behaved_context_run(", "native command graph player")
    require(behaved, "x2_override_004d9640", "retail BehavEd timed pump")
    require(behaved, "behaved_context_run(",
            "scheduler-to-native-context boundary")
    require(behaved, "behaved_player_step_context(",
            "ported context runner seam")
    require(behaved, "behaved_player_next_owned(",
            "ported owned scheduler seam")
    require(event, "x2_override_004b2d70", "retail timed-event pump")
    require(event, "cutscene_event_player_window_claim_new(",
            "causal event ownership")
    require(event, "cutscene_event_player_step_owned_slot(",
            "ported owned event seam")
    reject(player + behaved + event, "0x004d6a00", "cutscene player")
    reject(player + behaved + event, "0x004d9130", "cutscene player")
    reject(player + behaved + event, "WAIT_FLOOR_S", "cutscene player")
    reject(player + behaved + event, "deadline_clamped", "cutscene player")

    require(dialogue, "conversation_player_selection(",
            "deterministic conversation adapter")
    require(dialogue, "stop_active_voice(",
            "current dialogue cancellation")
    require(dialogue, "x2_override_00458700",
            "scoped beginResponse suppression")
    require(dialogue, "x2_override_0045a170",
            "scoped line-audio suppression")
    require(dialogue, "if (!g_dialogue.depth)",
            "ordinary dialogue super-call scope")
    require(dialogue, "fn_XMen2_00458700(cpu)",
            "ordinary dialogue retail path")
    require(dialogue, "suppressed_response_starts++",
            "silent response counter")
    require(dialogue, "suppressed_line_starts++",
            "silent line counter")
    reject(dialogue, "guest_clock", "cutscene dialogue policy")
    reject(dialogue, "world", "cutscene dialogue policy")

    require(script_audio, "x2_override_004a7130",
            "BehavEd sound command seam")
    require(script_audio, "cutscene_player_silences_current_context(&context)",
            "owned-context sound predicate")
    require(script_audio, "fn_XMen2_004a7130(cpu)",
            "ordinary retail sound path")
    require(script_audio, "g_silent_commands",
            "silent sound-command counter")
    reject(dsound, "cutscene_", "DirectSound backend")

    require(probe, "cutscene_player_snapshot(", "live probe")
    require(probe, "cutscene_dialogue_snapshot(",
            "silent-dialogue live probe")
    require(probe, "cutscene_script_audio_snapshot(",
            "silent-audio live probe")
    reject(probe, "conversation_cutscene_skip", "live probe")
    for source in (
        "src/native/behaved_player.c",
        "src/native/behaved_context.c",
        "src/native/cutscene_event_player.c",
        "src/native/conversation_player.c",
        "src/native/cutscene_dialogue.c",
        "src/native/cutscene_player_policy.c",
        "src/native/cutscene_player.c",
        "src/native/cutscene_script_audio.c",
    ):
        require(cmake, source, "x2native sources")
    reject(cmake, "src/native/conversation_cutscene_skip.c",
           "x2native sources")
    reject(cmake, "src/native/conversation_skip_policy.c",
           "x2native sources")


def production_sources():
    return (
        (ROOT / "src/native/conversation.c").read_text(),
        (ROOT / "src/native/cutscene_player.c").read_text(),
        (ROOT / "src/native/behaved_context.c").read_text(),
        (ROOT / "src/native/behaved_player.c").read_text(),
        (ROOT / "src/native/cutscene_event_player.c").read_text(),
        (ROOT / "src/native/cutscene_dialogue.c").read_text(),
        (ROOT / "src/native/cutscene_script_audio.c").read_text(),
        (ROOT / "src/native/dsound.c").read_text(),
        (ROOT / "src/native/cutscene_skip_probe.c").read_text(),
        (ROOT / "CMakeLists.txt").read_text(),
    )


def selftest():
    current = production_sources()
    audit(*current)
    discriminators = (
        (0, "uint32_t action = 4u"),
        (1, "x2_override_004a00d0"),
        (1, "CINEMATIC_SKIP_ACTION   20u"),
        (1, "x2_cutscene_player_finish("),
        (1, "behaved_player_next_owned("),
        (1, "cutscene_event_player_next_owned("),
        (1, "cutscene_dialogue_advance("),
        (1, "cutscene_dialogue_skip_begin("),
        (1, "cutscene_dialogue_skip_end("),
        (1, "cutscene_player_silences_current_context("),
        (1, "same_guest_time"),
        (2, "x2_override_004d8b30"),
        (2, "behaved_context_run("),
        (3, "x2_override_004d9640"),
        (3, "behaved_player_step_context("),
        (4, "x2_override_004b2d70"),
        (4, "cutscene_event_player_window_claim_new("),
        (5, "x2_override_00458700"),
        (5, "x2_override_0045a170"),
        (5, "fn_XMen2_00458700(cpu)"),
        (6, "x2_override_004a7130"),
        (6, "cutscene_player_silences_current_context(&context)"),
        (8, "cutscene_player_snapshot("),
        (8, "cutscene_dialogue_snapshot("),
        (8, "cutscene_script_audio_snapshot("),
        (9, "src/native/cutscene_player.c"),
        (9, "src/native/behaved_context.c"),
        (9, "src/native/cutscene_dialogue.c"),
        (9, "src/native/cutscene_script_audio.c"),
    )
    for index, needle in discriminators:
        broken = list(current)
        broken[index] = broken[index].replace(needle, "")
        try:
            audit(*broken)
        except WiringError:
            continue
        raise WiringError(
            f"negative discriminator passed after removing {needle!r}"
        )
    forbidden = (
        (0, "action = 20u"),
        (1, "0x004d6a00"),
        (3, "0x004d9130"),
        (7, "cutscene_"),
        (8, "conversation_cutscene_skip"),
        (9, "src/native/conversation_skip_policy.c"),
    )
    for index, needle in forbidden:
        broken = list(current)
        broken[index] += f"\n{needle}\n"
        try:
            audit(*broken)
        except WiringError:
            continue
        raise WiringError(
            f"negative discriminator admitted forbidden {needle!r}"
        )
    print(
        "cutscene_player_wiring --selftest: "
        f"{len(discriminators) + len(forbidden)} broken chains rejected"
    )


def main():
    if sys.argv[1:] == ["--selftest"]:
        selftest()
    elif sys.argv[1:]:
        raise SystemExit("usage: check_cutscene_player_wiring.py [--selftest]")
    else:
        audit(*production_sources())
        print(
            "cutscene_player_wiring: action20 -> cutscene player -> owned "
            "events/fibers; conversation is a deterministic subordinate payload"
        )


if __name__ == "__main__":
    try:
        main()
    except WiringError as error:
        raise SystemExit(f"cutscene_player_wiring: {error}") from None
