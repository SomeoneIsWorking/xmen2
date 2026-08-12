---
id: C156
kind: claim
status: holds
created: 2026-08-12
tags: native
---

## Claim

The port completes the whole game loop: main menu -> New Game -> cutscene -> level load -> gameplay -> the party dies -> the death dialog -> back to a fully rendered main menu.

## Evidence

One 300s scripted run (scratch/logs/loop.log, X2_INPUT_SCRIPT='140+250:Return,152+250:Return,170+300:Escape,182+300:Escape,245+300:Down,252+300:Return'), ending in scratch/screenshots/loop.png: the menu shows the logo, all six items with NEW GAME highlighted, the Sphinx, statue, columns, pyramids, steps and the QUIT button. Six scripted key presses, all six reported by the DirectInput 8 keyboard, which only prints from inside GetDeviceState -- so the game polled for each one. 0 draws refused.

## What would falsify it

the run is scripted against fixed TIMES, so a slower machine or a slower load would send Return while a different screen is up and the sequence would diverge without failing; if the final screenshot is not the menu, check the injection times against the heartbeat before concluding the loop broke
