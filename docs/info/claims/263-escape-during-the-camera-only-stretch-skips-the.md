---
id: C263
kind: claim
status: holds
created: 2026-08-25
tags: cutscene,conversation,input,continue
depends: src/native/conversation_cutscene_skip.c#conversation_cutscene_skip_observe_inactive, src/native/conversation_cutscene_skip.c#conversation_cutscene_skip_controls_locked, src/native/conversation_resume_policy.c#x2_conversation_resume_policy_observe, src/native/conversation_resume_policy.c#x2_conversation_resume_policy_expire, src/native/conversation.c#x2_override_0045d1a0, tools/live_case.py#case_cutscene_skip_early, tools/check_conversation_skip_wiring.py#audit
---

## Claim

Escape during the camera-only stretch skips the ENTIRE authored sequence, not just the visible record

## Evidence

tools/live_case.py cutscene-skip-early, 7/7 on the rebuilt scratch/build-native tree (2026-08-25). The probe before the press reported 'authored conversation: yes; visible no; camera-owned no; controls-locked yes; responses 0 (waiting)' and the log held ZERO 'conversation start' lines, so the press at frame 1047 landed in the opening camera pan before any record existed -- it reached the policy through conversation.c's disabled/invisible early-exit calls to conversation_cutscene_skip_observe_inactive, not through the action gate. The latch went ACTIVE on 1 request with 0 advances, then both records (1_introlevel_0020 and 0020b) started and were consumed: 5 retail response advance(s), 0 blocked, 0 ignored, conv_0020b_end launched, controls unlocked, latch idle. Two mechanisms make the sequence -- not the record -- the unit: observe_inactive arms the latch on an action-20 DOWN while the control lock holds, and x2_conversation_resume_policy_observe/_expire hold the policy across hidden gaps for as long as that lock holds (a fixed age bound cannot, since the walk between two records outlasts any constant). The first UNLOCKED hidden observation retires it, which is what keeps later unrelated dialogue untouched. The control case cutscene-skip (press on a visible line) still passes 7/7 with the same 5 advances.

## What would falsify it

an Escape in a camera-only locked stretch that leaves the latch at 0 request(s), or a run in which the latch is still ACTIVE after controls unlock, or ordinary unlocked dialogue after the sequence being advanced without a fresh press
