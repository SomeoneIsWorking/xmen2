---
id: 122
title: Cutscene skip targets payload waits instead of the cutscene player
status: resolved
symptom: One Escape request advances conversation records across many frames and globally shortens script deadlines instead of completing the owned in-game cutscene in one player invocation with guest time unchanged
tags: cutscene,behaved,scripts,player,timing,user-report
created: 2026-08-26
updated: 2026-08-27
state_items: S002,S012
---

## User requirement

Skip must execute the authored in-game cutscene to its natural control-return boundary in one cutscene-player invocation. It must not advance the guest clock, run world frames faster, or rewrite global script wait deadlines.

## Established root cause

The prior implementation was attached below the owner:
`conversation_cutscene_skip.c` advanced conversation responses per frame and
overrode scheduler insertion `FUN_004d6a00` to floor every wait while a latch
was active. The actual cutscene player composes the BehavEd timed-fiber pump
`FUN_004d9640`/runner `FUN_004d8b30` with the title timed-event pump
`FUN_004b2d70`; conversation records are subordinate payloads. `waittimed` at
`FUN_004d9130` only yields the current fiber. The tutorial is a control-lock
epoch spanning sibling fibers and timed events, with authored completion at
`conv_0020b_end` -> `lockControls(0.1)`.

## Required resolution

Port the player step and control-release semantics, make the skip operation own only the active cutscene epoch and its spawned fibers, preserve unrelated scheduled contexts, and delete the conversation-owned latch and global wait clamp. Verify one invocation, unchanged guest time/frame/world counters, exact authored side-effect order, and control available on return.

### Resolution (2026-08-27)
Replaced the conversation-owned latch and global wait-deadline floor with the title cutscene player. Ported ordinary/exact BehavEd pump 004d9640 and timed-event pump 004b2d70, tagged event ownership at exact insertion 004b2b40 through current BehavEd context 00787730, and restricted context inheritance to owned BehavEd parents, owned event callbacks, and deterministic conversation payloads. The player consumes only the final authored control delay without changing guest time. Silent windowless unbounded live gates pass: visible record 9/9 and camera-only 8/8, each one request/invocation/completion with same frame and guest clock.
