---
id: C156
kind: claim
status: falsified
created: 2026-08-12
tags: native
falsified_on: 2026-08-13
---

## Claim

The port completes the whole game loop: main menu -> New Game -> cutscene -> level load -> gameplay -> the party dies -> the death dialog -> back to a fully rendered main menu.

## Evidence

One 300s scripted run (scratch/logs/loop.log, X2_INPUT_SCRIPT='140+250:Return,152+250:Return,170+300:Escape,182+300:Escape,245+300:Down,252+300:Return'), ending in scratch/screenshots/loop.png: the menu shows the logo, all six items with NEW GAME highlighted, the Sphinx, statue, columns, pyramids, steps and the QUIT button. Six scripted key presses, all six reported by the DirectInput 8 keyboard, which only prints from inside GetDeviceState -- so the game polled for each one. 0 draws refused.

## What would falsify it

the run is scripted against fixed TIMES, so a slower machine or a slower load would send Return while a different screen is up and the sequence would diverge without failing; if the final screenshot is not the menu, check the injection times against the heartbeat before concluding the loop broke

### Update: the schedule no longer depends on the wall clock
The falsifier above (a script written against fixed TIMES is written against
one machine) was not hypothetical -- two runs of the SAME script on the SAME
machine reached frame 2639 at 140.03s and at 106.44s, a 25% spread. A slower
load would have put a Return into a different screen with nothing reporting a
failure.

`X2_INPUT_SCRIPT` now accepts `f<frames>[+<hold frames>]:<key>`, scheduling on
frames PRESENTED -- the game's own progress -- instead. The reproducible form
of the loop run is:

    X2_INPUT_SCRIPT="f2639+40:Return,f2815+40:Return,f3182+40:Escape,\
                     f3204+40:Escape,f4044+40:Down,f4135+40:Return"

which reaches the main menu on a run that took 189s where the time-based one
took 252s. Every injection line now reports both the time and the frame, so a
divergence can be attributed to one or the other.

## FALSIFIED 2026-08-13

After commit 5151a92 corrected explicit two-register FSUBR/FDIVR, the previously verified dense gameplay route no longer reaches the elimination/death dialog. It completes the opening Cyclops conversation and reaches IDirect3DDevice8::CreateVertexShader instead, proving the old route result depended on the reversed x87 arithmetic defect.

> Anything that cited this claim as proof must be re-checked. Grep the repo for it.
