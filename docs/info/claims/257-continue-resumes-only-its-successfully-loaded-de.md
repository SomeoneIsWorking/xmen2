---
id: C257
kind: claim
status: holds
created: 2026-08-24
tags: cutscene,conversation,continue
depends: src/native/conversation_resume.c#x2_conversation_resume_map_return, src/native/conversation_resume_policy.c#x2_conversation_resume_policy_observe, src/native/conversation_cutscene_skip.c#x2_override_004d9130, tests/test_conversation_timed_wait.c#main
---

## Claim

Continue resumes only its successfully loaded deterministic conversation sequence and never shortens a foreign script wait

## Evidence

Normal and NDEBUG policy tests prove successful-arm, ten-second expiry-before-classification, choice handback, manual override, and owned/unowned gaps. The production ABI test claims one SCRIPT_CONTEXT_RVA, shortens only that wait, super-calls foreign/malformed/expired waits, and pins the clock and RET 8 scheduler call contracts.

## What would falsify it

A failed or stale Continue arms resume, a choice is auto-advanced, or waittimed shortens a script context other than the exact claimed owner.
