---
id: C247
kind: claim
status: holds
created: 2026-08-22
tags: cutscene,player,input,live
depends: src/native/cutscene_player.c#finish, src/native/behaved_player.c#behaved_player_step_context, src/native/cutscene_event_player.c#cutscene_event_player_step_owned_slot, src/native/conversation_player.c#conversation_player_advance, tools/live_case.py#case_cutscene_skip
reconfirmed: 2026-08-27
verified_at: 2026-08-27
---

## Claim

Escape completes a deterministic gameplay-authored cutscene through its
authored scripts and returns control in one cutscene-player invocation without
advancing the guest frame or clock.

## Evidence

The rebuilt Clang tree passed `tools/live_case.py cutscene-skip --pacing fast`
9/9. Before input the cutscene player reported a live control-lock epoch and a
deterministic first conversation. One Escape launched `nightcrawler_spawn`,
crossed the timed-event player into `nightcrawler_walk`, started the adjacent
0020b conversation with a visible line, launched `conv_0020b_end`, consumed
the authored final control release, and retired the epoch. The final probe
reported `1 request(s), 1 invocation(s), 1 completion(s)`, 13 authored steps,
five deterministic conversation payloads, zero insertion faults, `1
same-frame`, and `1 same-guest-time`.

The port does not clear conversation flags, update the world, change the guest
clock, or clamp the global BehavEd scheduler. Deterministic records are
subordinate payloads selected through the retail response vtable transition.

## What would falsify it

A bounded authored scene bypasses a chosen/cleanup script, chooses across a
multi-response branch, leaves controls locked or the epoch active, runs a world
frame, changes the guest clock, or depends on a global scheduler-deadline
rewrite.
