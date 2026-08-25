---
id: 262
kind: claim
status: holds
created: 2026-08-25
tags: input,pad,hotswap,controller
depends: src/native/dinput8_hotplug.c#dinput8_hotplug_pump, src/native/dinput8_hotplug.c#dinput8_check_controller_table, src/native/dinput_pad_virtual.c#dinput_pad_virtual_from_env, src/input/player_input.c#resolve_pads
---

## Claim

A controller that first appears after the game started is adopted end to end
on a boot that did not load a save: the host's inventory generation bump
triggers one admission through the game's own enumeration routine, the pad is
resolved to its player by persistent id (no session assignment needed when
settings store a matching controller id), and its Start acts in gameplay
(opens the pause menu). The synthetic identity seam X2_VIRTUAL_PAD_ID makes
the persisted path testable at all, because SDL virtual pads carry no serial
or path.

## Evidence

tools/live_case.py pad-late 8/8 (transient assignment, Start opens the pause
menu, frame mean |delta| 22.7) and pad-persisted 7/7 (stored controller0.id
resolved to P1 with no session assignment, Start delta 22.7), both on a fresh
X2_BOOT_MAP boot, 2026-08-25.

## What would falsify it

A pad attached after start whose HOTSWAP line never appears, whose player
resolution stays keyboard/none despite a matching stored id, or whose Start
press the game never reads -- on a boot that has not loaded a save. (After a
SAVE load the poll side does not resume: that is issue #117, explicitly
outside this claim.)
