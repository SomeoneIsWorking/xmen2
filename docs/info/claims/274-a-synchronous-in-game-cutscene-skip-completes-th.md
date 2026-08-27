---
id: C274
kind: claim
status: holds
created: 2026-08-27
tags: cutscene,audio,native,issue-126
depends: src/native/cutscene_player.c#finish, src/native/cutscene_dialogue.c#cutscene_dialogue_advance, src/audio/audio_play_policy.c#audio_play_policy_allow_start, src/native/dsound.c#b_Play
reconfirmed: 2026-08-27
verified_at: 2026-08-27 03:07:49
---

## Claim

A synchronous in-game cutscene skip completes the owned epoch without starting cutscene audio: exact dialogue presenters are suppressed and every reached DirectSound Play is refused until the scope retires.

## Evidence

Windowless dummy-audio unbounded/unpaced live_case cutscene-skip passed 11/11: one active voice stopped, 5 response and 4 line starts suppressed, 2 additional DirectSound starts suppressed, zero dialogue leaks, balanced scope, same frame/time. cutscene-skip-early passed 10/10: 5/5 dialogue starts and 2 DirectSound starts suppressed with the same invariants. test_cutscene_dialogue and test_audio_play_policy provide ordinary-playback positive controls and skip falsifiers.

## What would falsify it

if any synchronous cutscene-player invocation starts audible dialogue/SFX, reports a nonzero dialogue leak or nonzero final suppression depth, advances guest frame/time, or if ordinary playback no longer reaches the retained presenters and DirectSound Play

## Re-confirmed 2026-08-27

Reconfirmed at commit dc892cf after the combined Clang build: live_case cutscene-skip passed 11/11 and cutscene-skip-early passed 10/10 windowless, dummy-audio, unbounded/unpaced; each suppressed two DirectSound starts, exact dialogue counters showed zero leaks, the scope retired at depth zero, and frame/time were unchanged. All 118 CTest gates passed (optional external-asset FMV decode skipped).
