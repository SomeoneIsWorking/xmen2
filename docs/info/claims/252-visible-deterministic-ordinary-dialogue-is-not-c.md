---
id: C252
kind: claim
status: holds
created: 2026-08-22
tags: cutscene,conversation,regression
depends: src/native/conversation_cutscene_skip.c#snapshot, src/native/conversation_skip_policy.c#conversation_skip_policy_is_authored, src/native/conversation_skip_policy.c#conversation_skip_policy_update, tests/test_conversation_skip_policy.c, tools/check_conversation_skip_wiring.py#audit
reconfirmed: 2026-08-22
verified_at: 2026-08-22 17:13:54
---

## Claim

Visible deterministic ordinary dialogue is not classified as an authored cutscene and ignores Escape or Start

## Evidence

The production conversation snapshot now calls conversation_skip_policy_is_authored, whose only evidence inputs are parsed camera ownership and the retail controls-lock state. test_conversation_skip_policy drives visible=1, camera_owned=0, controls_locked=0, skip_pressed=1, and a deterministic single response through that production classifier and policy: the result is NONE, ignored increments, and the latch remains inactive. The same test retains an already-active authored sequence across an invisible controls-locked gap. Focused policy and wiring ctests pass, and the wiring selftest rejects removal of the production classifier call.

## What would falsify it

Falsified if visibility alone can make the production snapshot authored, a visible ordinary deterministic dialogue arms or advances the skip latch, or an active authored sequence fails to survive an invisible controls-locked gap.

## Re-confirmed 2026-08-22

Reconfirmed on the final working tree: the production classifier requires camera ownership or controls lock; the visible ordinary single-response regression is ignored without arming, and the invisible controls-locked authored-gap regression retains the latch. Focused policy/wiring tests and six wiring discriminators pass.
