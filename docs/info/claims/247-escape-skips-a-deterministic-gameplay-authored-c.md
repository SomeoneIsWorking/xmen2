---
id: C247
kind: claim
status: holds
created: 2026-08-22
tags: cutscene,conversation,input
depends: src/native/conversation.c#x2_override_0045d1a0, src/native/conversation_cutscene_skip.c#snapshot, src/native/conversation_cutscene_skip.c#conversation_cutscene_skip_should_advance, src/native/conversation_cutscene_skip.c#conversation_cutscene_skip_observe_inactive, src/native/conversation_skip_policy.c#conversation_skip_policy_is_authored, src/native/conversation_skip_policy.c#conversation_skip_policy_update, src/native/cutscene_skip_probe.c#cutscene_skip_probe_report, tools/check_conversation_skip_wiring.py#audit
reconfirmed: 2026-08-22
verified_at: 2026-08-22 17:13:43
---

## Claim

Escape skips a deterministic gameplay-authored conversation through retail cleanup and retires the latch after controls return

## Evidence

Bounded X2_BOOT_MAP=act0/tutorial/tutorial1 live run on the rebuilt fix: pre-input probe reported authored yes, visible yes, camera-owned yes, controls-locked yes, one deterministic response. Escape at frame 1026 produced one request and five retail response advances; I064 named nightcrawler_spawn at frame 1027 and conv_0020b_end at frame 1238. The post-input probe reported visible no, controls-locked no, authored skip idle. The earlier committed run produced the same scripts/control cleanup but left the latch ACTIVE, validating that the new inactive early-exit observation can show the opposite result rather than merely being present.

## What would falsify it

A bounded authored scene bypasses response/chosen scripts, reaches a branch without blocking, fails to restore authored camera/control state, or leaves the skip latch active after controls return

## Re-confirmed 2026-08-22

Reconfirmed against the final inactive-exit implementation and bounded tutorial rerun: Escape at frame 1026 produced one request/five retail advances, launched nightcrawler_spawn and conv_0020b_end, restored controls, and the live probe reported the authored skip idle.

## Re-confirmed 2026-08-22

The tightened final classifier still admits the verified tutorial because its pre-input probe reported both camera ownership and controls lock; the Escape run's five retail advances, cleanup scripts, restored controls, and idle latch remain valid.
