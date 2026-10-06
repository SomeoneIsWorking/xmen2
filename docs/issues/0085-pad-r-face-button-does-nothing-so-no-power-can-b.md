---
id: 85
title: Pad: R + face button does nothing, so no power can be used in gameplay
status: resolved
symptom: holding R and pressing X/A/Y/B in gameplay triggers no power -- the character does nothing, while the same buttons work in menus
tags: pc,native,input,pad,bindings,gameplay,user-report
created: 2026-08-19
updated: 2026-08-20
---

REPORTED BY THE USER, 2026-08-19, from a real play session with a pad.
Reproduced and resolved below on 2026-08-20.

## The symptom

In gameplay, **R + X / R + A / R + Y / R + B do nothing**: no power fires, and
abilities are unusable with the pad. This is the console power-select idiom --
hold the right shoulder as a modifier, then a face button chooses the power --
and it is how the Xbox build binds the four powers.

The pad is otherwise live: it enumerates, its sticks move the character, and
face buttons work in the menus (that is what commit c956129/12cc30d captured).
So this is NOT the host half (`SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS`,
issue #82) and not the pad reaching DirectInput at all.

## Why the earlier input checks did not cover this

The port's Xbox default mapping (`src/native/xbox_defaults.c`, feature 2)
installs a set of rows into the game's binding banks. C215 established how the
game reads them -- the working copies 4..7, filled from masters 0..3 by
`FUN_0061b030`, with the preset going into slot 1, the alternate the game
leaves free. What was VERIFIED after that fix is narrow and it is worth
rereading before assuming anything: pad A sets player 0's `physical[4]`, three
presses carry the tutorial conversation, and `leftx=-1` sets `physical[0]`.
**Every one of those is a menu/dialog or a movement action.** No POWER row was
ever checked.

That left two separate questions: whether the `Power` row delivered RT at full
scale, and whether the game combines that modifier with the four face-button
attack rows. `tools/x2ctl.py input` measured the first without guessing from a
screenshot; the initialized boot-map gameplay capture settled the second.

## The trigger delivered nothing, so the Power modifier never engaged

`Power` is binding row 8, and the port's preset binds it to physical code 0x06
-- Z-, the right trigger. Two defects in `src/native/dinput_pad.c` meant that
code could never resolve to anything:

* **Scale.** The shared Z axis was `(l - r) / 2`. One trigger squeezed alone
  reached HALF the range the game sets with DIPROP_RANGE, where a fully
  deflected stick reaches all of it. Real hardware included.
* **Rest, synthetic pad only.** SDL maps a virtual joystick axis's whole
  -32768..32767 travel onto a trigger's 0..32767, so a fresh virtual axis (0)
  presents the trigger HALF HELD. Both at once, cancelling to centre on the
  shared axis -- which is why nothing looked wrong -- and the timed release put
  them back to 0 rather than to the minimum, so any run that ever pressed a
  trigger left it stuck half down.

Measured through the game's own accessor after the fix: idle reads 0 of 52
physical codes non-zero, and with the right trigger held, code 0x06 resolves to
**+1.000** and player 0's physical array shows `[7]=1.000 [22]=1.000` -- the two
action slots that map to row 8. Before, the same probe read zero in both states.

The probe that shows this is new and is why it was findable: `x2ctl.py input`
now samples **every** physical code with its MAGNITUDE, not four face buttons
with a down/up bit. An analog code arriving at half scale is invisible to a
boolean.

## Quick-power rows are a separate keyboard feature

The binding table dump says the quick-power rows are **entirely unbound on the
pad**. Of the 42 rows, the preset now covers 22; the ones it does not include

    29 BindPower   30 UseQuickPower   31..41 QuickPower01..11

plus Solo, Talk, Walk, SwtHero, AttackObject, RotateCamera. Each of those has a
keyboard default in slot 0 and nothing in the pad slot -- e.g.
`UseQuickPower` is DIK 0x29 (grave) and `QuickPower01..04` are the `1`..`4`
keys.

They are not the console mechanism. `Power` row 8 is the modifier and gameplay
combines it with the face-button attack rows. Held RT+A produced codes `0x06`
and `0x15` at `+1.000`, then the selected hero entered and completed the cast
animation. C218's old reason for excluding boot-map runs is falsified: boot-map
now runs the retail party initializer and resolves a hero. The unbound
quick-power rows therefore do not block the Xbox-style gesture and are not a
reason to invent eleven pad assignments.

### Resolution (2026-08-20)
Verified in a normal boot-map gameplay run with the retail party initializer:
held RT+A delivered physical codes `0x06` and `0x15` at `+1.000`, and
synchronized frames captured the selected hero entering and completing the
power-cast animation. The trigger delivery fix in C222 plus the Xbox default
Power/LowAttack rows is the complete path; quick-power rows are not required
for the console modifier gesture.
