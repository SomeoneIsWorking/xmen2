---
id: C222
kind: claim
status: holds
created: 2026-08-19
tags: input,pad,triggers
depends: src/native/dinput_pad.c#dinput_pad_axis, src/native/dinput_pad.c#attach_virtual_pad
---

## Claim

The pad's triggers delivered NOTHING to the game: they rested half-squeezed and cancelled on their shared axis, and a single trigger only ever reached half scale

## Evidence

Two independent defects in src/native/dinput_pad.c, both measured live through the game's OWN accessor FUN_00627650 and its per-player physical array. (1) SCALE: the shared Z axis was computed as (l - r) / 2, so one trigger squeezed alone reached HALF the range the game set with DIPROP_RANGE, where a fully deflected stick reaches all of it. (2) REST, synthetic pad only: SDL maps a virtual joystick axis's whole -32768..32767 travel onto a trigger's 0..32767, so a fresh virtual axis (0) presents the trigger HALF HELD -- both at once, cancelling to centre on the shared axis, and the timed-release path put them back to 0 rather than to the minimum. After both fixes, on a boot-map run with the synthetic pad: idle reads 0 of 52 physical codes non-zero, and with righttrigger held, code 0x06 (Z-) resolves to +1.000 exactly and the game's player 0 physical array shows [7]=1.000 and [22]=1.000 -- the two action slots that map to binding row 8, Power. Before the fixes the same probe read 0 of 52 idle AND 0 with the trigger held, because the two half-held triggers cancelled.

## What would falsify it

a run where a real (non-synthetic) Xbox pad's trigger resolves to something other than 1.000 at full squeeze, or where the game's power logic is shown to read a code other than row 8's
