---
id: C241
kind: claim
status: holds
created: 2026-08-22
tags: input,cutscene,conversation
depends: src/native/conversation.c#x2_override_0045d1a0, src/native/conversation_cutscene_skip.c#conversation_cutscene_skip_should_advance, src/native/conversation_skip_policy.c#conversation_skip_policy_update, tools/check_conversation_skip_wiring.py#audit
---

## Claim

The host authored-conversation skip never clears cinematic or conversation flags: it arms action 20 while an authored conversation is visible (with camera/control state covering preparation and locked gaps), stops at a response branch, and dispatches only deterministic advances through the retail chooseResponse/applyResponse path that launches authored cleanup scripts.

## Evidence

Static XMen2.exe decompilation: FUN_0045d1a0 consumes Accept through vtable +0x138 and calls conversation vtable +0x18; FUN_0045cde0 launches chosenScriptFile on normal, ending, and tag-jump exits; lockControls handler 0x0049f8c0 writes the clock object control deadline through vtable +0x2c. The production implementation reuses that vtable +0x18 path. test_conversation_skip_policy passed 18 checks covering ignored, waiting, adjacent locked segments, deterministic advance, choice block, unreadable block, and reset. check_conversation_skip_wiring verified action20 -> production policy -> retail chooseResponse plus the production probe/x2native source list and rejected four deliberately broken chains. Production sources passed -Wall -Wextra -Werror syntax checks, Python lint, and check_structure.

## What would falsify it

A bounded authored scene shows Escape/Start clearing or bypassing a response/chosen cleanup script, choosing across a multi-response branch, failing to advance action 20, or continuing to skip after lockControls returns control.
