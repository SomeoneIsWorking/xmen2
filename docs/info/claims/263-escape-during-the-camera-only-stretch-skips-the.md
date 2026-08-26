---
id: C263
kind: claim
status: holds
created: 2026-08-25
tags: cutscene,player,input,live
depends: src/native/cutscene_player.c#x2_override_004a00d0, src/native/cutscene_player.c#begin_sequence, src/native/behaved_player.c#behaved_player_step_context, src/native/cutscene_event_player.c#x2_override_004b2b40, tools/live_case.py#case_cutscene_skip_early, tools/check_cutscene_player_wiring.py#audit
reconfirmed: 2026-08-27
verified_at: 2026-08-27
---

## Claim

Escape during the camera-only locked stretch completes the entire authored
cutscene, proving the cutscene player rather than a visible conversation owns
the operation.

## Evidence

The rebuilt Clang tree passed `tools/live_case.py cutscene-skip-early --pacing
fast` 8/8. The pre-input boundary was `controls locked; conversation payload
inactive`, and the log contained zero conversation starts. One Escape completed
the root BehavEd fiber, both later conversations, the timed spawner transition,
and `conv_0020b_end`; controls returned and the epoch retired. The final report
recorded one request/invocation/completion with the presented frame and guest
clock unchanged. Both conversation records started only after the press and
were consumed inside that invocation.

## What would falsify it

An action-20 edge during a locked camera-only stretch needs a later world frame
to reach a conversation or cleanup, leaves the epoch active after control
release, or advances later unlocked dialogue without a fresh press.
