---
id: 83
title: The tutorial soft-locks after the cutscene: the second conversation's first line is suppressed by an already-set seen bit
status: investigating
symptom: After the opening conversation the tutorial never hands control back: no HUD, the camera stays on cam_prof, and the character does not move although input reaches player 0 (physical[0] = -1.000 with the stick held). The game keeps rendering at ~68 fps, so it is not a hang.
tags: pc,native,gameplay,tutorial,conversation,scripts,softlock
created: 2026-08-19
updated: 2026-08-19
---

## What the soft lock IS

`tutorial1.py` opens the level with

    lockControls(-1.000)
    setallaiactive("FALSE")
    cameraFocusToEntity("cam_prof", ...)
    startConversation("act0/tutorial/tutorial1/1_introlevel_0020")

and the only thing that undoes it is `conv_0020b_end.PY`:

    cameraReset()
    cameraFade(0.000, 1.000)
    lockControls(0.100)
    setallaiactive("TRUE")

That script never runs, so the controls stay locked forever. The screen agrees:
the camera is still the fixed `cam_prof` framing and there is no HUD. Input is
NOT the problem -- with the stick held, player 0's physical[0] reads -1.000.

## The chain, and exactly where it stops

Measured with the new script trace (`X2_SCRIPTS=1`), which names every
BehavEd script the run launches:

    tutorial1            frame 2385   -> lockControls, startConversation 0020
    conversation 0020 plays and ends on the player's accept
    nightcrawler_spawn   frame 4346   (the conversation's chosenscriptfile)
    nightcrawler_walk    frame 4431   (the spawner's own script)
    ... and nothing further

`nightcrawler_walk.PY` ends with
`startConversation("act0/tutorial/tutorial1/1_introlevel_0020b")`, and
`conv_0020b_end` is 0020b's chosenscriptfile. So the whole unlock hangs off
0020b starting.

It runs to its last statement -- the professor is gone from the chair on
screen, which is that script's `remove("px", "px")`, the line before the
`startConversation`.

## The command runs, the manager accepts it, and no line is selected

    conversation start "1_introlevel_0020"  -> STARTED  flags 0x10 -> 0x13, line 0x00000000 -> 0x00000040
    conversation start "1_introlevel_0020b" -> STARTED  flags 0x18 -> 0x10, line 0x00000041 -> 0x00000000

`igConversationManager::start` (vtable +0x14, FUN_0045c950) returns TRUE for
0020b and selects NO line: it clears the ENDING bit (that IS the 0x18 -> 0x10)
and leaves speaking/visible clear. `nextLine` is never called again.

## The cause, from a region recording of both calls

`recomp.py emit --record 0x0045c460-0x0045c946` over two passes -- pass 1 the
conversation that works, pass 2 the one that does not -- diverges at ONE
instruction, step 83 of 608/110:

    0045c58a  TEST dword ptr [ECX + EAX*0x4 + 0x21b48],EDX
    0045c591  JZ 0x0045c59e
      pass 1 (0020, works)  -> taken, the line plays
      pass 2 (0020b, fails) -> not taken, the line is skipped

`+0x21b48` on the conversation singleton is a 160-bit "this entry has already
been said" bitmap. FUN_0045c460 tests one bit per entry and sets it after
playing (0x0045c6a0), and the index is `[ESP+0x14] + arg1*4` -- a counter that
restarts at 0 inside each conversation, plus a base the caller supplies.

BOTH conversations compute **bit 0**: both reach FUN_0045c460 through the
fallback call site at 0x0045cceb, which passes the base as a literal `PUSH 0x0`.

Watched directly across the two starts:

    0020  seen bitmap 00000000 ... -> 00000001 ...     (plays, sets bit 0)
    0020b seen bitmap 00000001 ... -> 00000001 ...     (bit 0 already set, skipped)

## Why nothing clears it between the two

The bitmap is cleared only by convmgr vt+0x50 (FUN_00458090), whose only caller
is vt+0x04 (FUN_00455af0), which also sets the ENABLED flag. Counted over the
run: it fires **once**, at frame 2407, before the first conversation, and never
again. Its two call sites are big level/zone reset routines (FUN_00469950,
FUN_00484ce0), not anything per-conversation.

`endConversation` (FUN_004585f0) has exactly one caller -- the update's
"no slot left" branch at 0x0045d22c -- and the update returns early when the
conversation is not visible, which it is not once applyResponse sets ENDING.
That early-out is FAITHFUL: the original tests isVisible at 0x0045d1f9 and
returns, before any ENDING test, exactly as the port does.

## What is NOT established

Whether this is a port defect or the shipped game's own behaviour. Everything
above says the two conversations legitimately collide on bit 0 given the code
as written, which cannot be how the retail game behaves -- so something upstream
must differ. The prime suspect is the branch that picks the call site:

    0045cade  MOV EAX,dword ptr [ESP + 0x38]     ; the speaker actor
    0045cae3  CALL 0x00402b60                    ; dynamic cast
    0045caed  TEST EBX,EBX
    0045caef  JNZ 0x0045cb27                     ; -> the OTHER call site, with a real base

In both of our passes the actor is 0, so the cast returns 0 and the base-0
fallback is taken. If the retail game resolves a speaker there, it uses
0x0045cbc3 instead and the bases differ.

**The next step is the oracle, not more reading**: run the stock install under
Wine to the same point and see whether 0020b plays. If it does, capture whether
its speaker actor resolves. Do NOT clear the bitmap or special-case the second
conversation -- that would be a bandaid over an unidentified cause.

## Tools this produced

* `src/native/script_trace.c` -- every script launch by name, plus
  startConversation/lockControls/conversation-start/reset with their results.
* `tools/script_commands.py` -- the 289-entry BehavEd command table.
