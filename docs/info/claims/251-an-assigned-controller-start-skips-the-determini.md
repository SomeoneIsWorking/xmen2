---
id: C251
kind: claim
status: holds
created: 2026-08-22
tags: cutscene,input,live
depends: src/native/conversation.c#x2_override_0045d1a0, src/native/conversation_cutscene_skip.c#snapshot, src/native/conversation_cutscene_skip.c#conversation_cutscene_skip_should_advance, src/native/conversation_cutscene_skip.c#conversation_cutscene_skip_observe_inactive, src/native/conversation_skip_policy.c#conversation_skip_policy_is_authored, src/native/conversation_skip_policy.c#conversation_skip_policy_update, src/input/player_input.c#x2_player_input_publish, src/native/cutscene_skip_probe.c#cutscene_skip_probe_report, src/native/script_runtime.c
reconfirmed: 2026-08-22
verified_at: 2026-08-22 17:13:43
---

## Claim

An assigned controller Start skips the deterministic tutorial conversation through the retail response and cleanup chain

## Evidence

A bounded X2_BOOT_MAP=act0/tutorial/tutorial1 run with the session-only X2 Virtual Pad assigned to Player 1 reported Escape and Start on Pause row 17 in master/working/menu (3/3 each). At frame 15121 the control channel's Start press made SDL gamepad Start read DOWN. The authored skip recorded one request and five retail response advances; nightcrawler_spawn launched at frame 15122, nightcrawler_walk at 15183, the adjacent 1_introlevel_0020b conversation started at 15333, and conv_0020b_end launched at 15335. By frame 15841 the conversation was hidden, controls were unlocked, and the authored skip latch was idle.

## What would falsify it

Falsified if an assigned controller's Start is absent from any published Player 1 bank, does not produce action 20 at an authored deterministic conversation, bypasses the retail response/chosen-script calls, leaves controls locked, or leaves the authored skip latch active after cleanup.

## Re-confirmed 2026-08-22

Reconfirmed after the final combined build: an assigned session-only controller published Start in all three Player 1 banks; Start at frame 15121 drove five retail advances, the expected adjacent conversation/cleanup scripts, restored controls, and left the skip latch idle by frame 15841.

## Re-confirmed 2026-08-22

The tightened final classifier still admits the verified tutorial because the controller run's pre-input probe reported both camera ownership and controls lock; assigned Start still drove five retail advances, cleanup scripts, restored controls, and an idle latch.
