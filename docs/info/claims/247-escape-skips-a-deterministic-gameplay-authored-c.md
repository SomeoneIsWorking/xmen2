---
id: C247
kind: claim
status: holds
created: 2026-08-22
tags: cutscene,conversation,input
depends: src/native/conversation_cutscene_skip.c#conversation_cutscene_skip_observe_inactive
reconfirmed: 2026-08-25
verified_at: 2026-08-25 14:19:20
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

## Re-confirmed 2026-08-25

Re-verified 2026-08-25 after the observe_inactive signature gained the live input manager and the resume policy gained the control-lock hold: tools/live_case.py cutscene-skip 7/7 on the rebuilt scratch/build-native tree. Escape at frame 1265 on a visible line produced 1 request and 5 retail response advances, launched nightcrawler_spawn and conv_0020b_end, 0020b started 0x18->0x13 with line 0x41->0x40, controls unlocked, latch idle. The whole-sequence behaviour this now composes with is C263.
