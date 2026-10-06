---
id: C257
kind: claim
status: falsified
created: 2026-08-24
tags: cutscene,conversation,continue
depends: src/native/conversation_resume.c#x2_conversation_resume_map_return, src/native/conversation_resume_policy.c#x2_conversation_resume_policy_observe, src/native/conversation_cutscene_skip.c#x2_override_004d9130, tests/test_conversation_timed_wait.c#main
falsified_on: 2026-08-25
---

## Claim

Continue resumes only its successfully loaded deterministic conversation sequence and never shortens a foreign script wait

## Evidence

Normal and NDEBUG policy tests prove successful-arm, ten-second expiry-before-classification, choice handback, manual override, and owned/unowned gaps. The production ABI test claims one SCRIPT_CONTEXT_RVA, shortens only that wait, super-calls foreign/malformed/expired waits, and pins the clock and RET 8 scheduler call contracts.

## What would falsify it

A failed or stale Continue arms resume, a choice is auto-advanced, or waittimed shortens a script context other than the exact claimed owner.

## FALSIFIED 2026-08-25

The first default-path user run after 82bdf13 reached Escape in DirectInput and advanced the first conversation, then the adjacent conversation entered flags 0x10 with no visible line, the exact issue #83 softlock signature. 82bdf13's waittimed override had replaced retail actor/movement delays with immediate scheduling; the last live-working Escape implementation preserved those waits. The global wait acceleration and its unit-only claim are removed.

> Anything that cited this claim as proof must be re-checked. Grep the repo for it.

## Removal verified 2026-08-25

The waittimed override and its test are gone; the wiring audit rejects any
reintroduced 0x004d9130 override. The Escape chain was re-proven live on the
rebuilt tree (cutscene-skip 7/7, C247's shape) and the Continue-resume
softlock's true root cause turned out to be the cleared current player, not
wait timing -- see C261 and issue #113.
