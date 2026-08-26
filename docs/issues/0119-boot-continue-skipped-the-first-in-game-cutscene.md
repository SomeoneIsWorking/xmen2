---
id: 119
title: Boot Continue skipped the first in-game cutscene with no key pressed
status: resolved
symptom: With boot.mode=continue, ./run.sh loads the first level and the opening cutscene fast-forwards by itself; the player pressed nothing. Escape-to-skip is supposed to be the only way to skip an authored cutscene.
tags: pc,native,continue,conversation,cutscene,scripts,user-report
created: 2026-08-26
updated: 2026-08-26
---

REPORTED BY THE USER, 2026-08-26.

## Symptom

`./run.sh` with `boot.mode=continue` loads the saved first level, and the
opening authored cutscene is skipped without any input. The user asked for
Escape to skip in-game cutscenes; this happened on its own.

## Root cause

`x2_conversation_resume_map_return()` armed an automatic skip on EVERY
successful Continue map return (`src/native/autosave_runtime.c`), with
nothing testing whether the restored save had actually been taken inside a
conversation. For ten guest-clock seconds afterwards the armed policy did
two things:

* auto-advanced deterministic conversation records through the retail
  `chooseResponse` path, and
* satisfied `wait_scope_allows()` in `conversation_cutscene_skip.c`, whose
  condition was `g_policy.active || x2_conversation_resume_sequence_active()`
  -- so the script scheduler wait insert (`FUN_004d6a00`) was floor-clamped
  to 0.10s and the scene lost its authored pacing.

Reproduced with ZERO input (`X2_MAX_FRAMES=2500 x2native --no-window --d3d8
--run`, `scratch/logs/continue-repro-run.log`):

    Continue resume: 1 armed, 5 retail advance(s), 0 manual override(s)
    authored skip idle: 171 action check(s), 0 DOWN; 0 request(s);
                        script waits 9/10 floor-limited (0.10s), scope armed

`0 DOWN` and `0 request(s)` prove the Escape path was not involved at all.

The mechanism came from issue #113, on the assumption that Continue returns
into an ALREADY-SEEN conversation. That assumption does not hold for the
post-map autosave (#99), which is taken at level start -- so the opening
cutscene has never been seen and falls inside the ten-second window. Note
that #113s actual softlock root cause was the player-index/handle fix at the
LOAD SUCCESSFUL ack, not this resume; the resume was a convenience layer on
top.

## Resolution

USER, 2026-08-26: *"There shouldnt be anything done after load, Continue is
only supposed to load the latest save and thats it"*.

The whole resume mechanism is DELETED -- `conversation_resume.{c,h}`,
`conversation_resume_policy.{c,h}`, their unit test, the CMake wiring, the
`autosave_runtime.c` arm, the `continue_runtime.c` pending/cancel calls, the
`conversation.c` observe/advance/report calls, and the
`x2_conversation_resume_sequence_active()` term in `wait_scope_allows()`.
Continue now loads the save and does nothing else.

Verified on the same zero-input Continue boot
(`scratch/logs/continue-fixed-run.log`):

    authored skip idle: 2491 action check(s), 0 DOWN; 0 request(s),
                        0 retail response advance(s);
                        script waits 0/4 floor-limited, scope idle

Manual Escape is untouched: `tools/live_case.py cutscene-skip` passes 7/7
with `1 request(s), 5 retail response advance(s), 6/10 floor-limited`, the
adjacent conversation visible with a line and controls unlocked.

## The gate this broke, and why that was the gate being wrong

`tools/live_case.py boot-continue` failed 12/14 at first: it asserted the
adjacent conversation `1_introlevel_0020b` had started BEFORE it pressed
Escape. That ordering only ever passed because the auto-resume advanced the
records with no input -- the case had encoded the bug as its expectation.
With retail pacing restored, 0020b arrives tens of thousands of frames later,
on the far side of the press. The case now advances the scene the way a
player does and then asserts: 14/14, including the issue #83 seen-bit check,
so the #113 player-selection fix remains verified.
