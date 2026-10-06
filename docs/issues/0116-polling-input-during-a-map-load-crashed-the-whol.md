---
id: 116
title: Polling /input during a map load crashed the whole run: the probe's slot lookup stricmp'd NULL names in a half-built conversation table
status: resolved
symptom: A passive /input probe poll during level load killed x2native with SIGSEGV at (nil) inside _stricmp; three polls during the tutorial load reproduced it every time
tags: pc,native,input,probe,crash,conversation,diagnostic,instrument
created: 2026-08-25
updated: 2026-08-25
---

## Root cause

conversation_cutscene_skip.c's current_slot() guest-called the retail slot
lookup (0x00456440) speculatively on every /input poll, at any moment. That
retail code stricmps the NAME FIELD of every conversation-table node it walks,
and a manager that has not started a conversation yet still holds NULL names --
so a poll landing during map construction faulted inside host strcasecmp
(crt.c imp_MSVCR71__stricmp). Production never does this: the conversation
update override reaches FN_SLOT_OF only after its own visible gate.

## Evidence

- 3 /input polls during the X2_BOOT_MAP tutorial load killed the run (rc=3),
  deterministic, paced or unpaced.
- gdb backtrace: control_pump -> input_probe_report -> cutscene_skip_probe ->
  current_slot -> fn_XMen2_00456440 -> imp_MSVCR71__stricmp ->
  __strcasecmp_l_avx2(NULL).
- The same binary without --control, or under gdb without polling, ran the
  same load to completion repeatedly.

## Fix

current_slot() now peeks the conversation flags first and only executes the
retail lookup when a conversation is VISIBLE -- the same production ordering.
An idle or half-built manager reports SLOT_NONE without any guest call.

## Falsifier

A /input poll during a map load that crashes, or an authored visible
conversation whose slot the probe reports as waiting/none.
