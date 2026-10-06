---
id: C274
kind: claim
status: holds
created: 2026-08-27
tags: cutscene,audio,native,issue-126
depends: src/native/cutscene_player.c#finish, src/native/behaved_context.c#behaved_context_run, src/native/cutscene_dialogue.c#cutscene_dialogue_advance, src/native/cutscene_script_audio.c#x2_override_004a7130
reconfirmed: 2026-08-27
verified_at: 2026-08-27 20:46:50
---

## Claim

A synchronous in-game cutscene skip completes the owned epoch without starting its reached audio: exact dialogue presenters and the exact BehavEd sound handler are suppressed only for player-owned work, while DirectSound remains ordinary.

## Evidence

Windowless timed-silent unbounded/unpaced `live_case cutscene-skip` passed 11/11: one active voice stopped, 5 response and 4 line starts suppressed, both authored BehavEd sound commands consumed silently in an owned context, zero dialogue leaks, and the same frame/time. `cutscene-skip-early` passed 10/10 with 5/5 dialogue starts and both sound commands suppressed under the same invariants. `test_cutscene_dialogue` and `test_cutscene_script_audio` provide ordinary/foreign positive controls and skip falsifiers; `test_behaved_context` exercises the native command player.

## What would falsify it

if any synchronous cutscene-player invocation starts reached dialogue or BehavEd sound audio, reports a nonzero dialogue leak, advances guest frame/time, consumes post-control-release work, or if ordinary/foreign execution no longer reaches the retained presenters

## Re-confirmed 2026-08-27

Superseded by the 2026-08-27 native `004d8b30` context-player port and exact `004a7130` BehavEd sound seam described above; the earlier DirectSound-wide evidence no longer names the shipping ownership boundary.

## Re-confirmed 2026-08-27

Native 004d8b30 BehavEd context player plus exact 004a7130 sound seam: Clang build and 119 CTest entries passed (FMV asset test skipped); windowless timed-silent unbounded/unpaced live_case cutscene-skip passed 11/11 and cutscene-skip-early passed 10/10, with 0 ordinary and 2 silent script-sound commands, zero dialogue leaks, controls restored, and unchanged guest frame/time.
