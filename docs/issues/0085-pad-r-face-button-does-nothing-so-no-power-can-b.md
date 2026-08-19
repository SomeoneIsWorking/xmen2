---
id: 85
title: Pad: R + face button does nothing, so no power can be used in gameplay
status: open
symptom: holding R and pressing X/A/Y/B in gameplay triggers no power -- the character does nothing, while the same buttons work in menus
tags: pc,native,input,pad,bindings,gameplay,user-report
created: 2026-08-19
updated: 2026-08-19
---

REPORTED BY THE USER, 2026-08-19, from a real play session with a pad. Not yet
reproduced by an agent.

## The symptom

In gameplay, **R + X / R + A / R + Y / R + B do nothing**: no power fires, and
abilities are unusable with the pad. This is the console power-select idiom --
hold the right shoulder as a modifier, then a face button chooses the power --
and it is how the Xbox build binds the four powers.

The pad is otherwise live: it enumerates, its sticks move the character, and
face buttons work in the menus (that is what commit c956129/12cc30d captured).
So this is NOT the host half (`SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS`,
issue #82) and not the pad reaching DirectInput at all.

## Why this is very likely the preset, not the pad

The port's Xbox default mapping (`src/native/xbox_defaults.c`, feature 2)
installs a set of rows into the game's binding banks. C215 established how the
game reads them -- the working copies 4..7, filled from masters 0..3 by
`FUN_0061b030`, with the preset going into slot 1, the alternate the game
leaves free. What was VERIFIED after that fix is narrow and it is worth
rereading before assuming anything: pad A sets player 0's `physical[4]`, three
presses carry the tutorial conversation, and `leftx=-1` sets `physical[0]`.
**Every one of those is a menu/dialog or a movement action.** No POWER row was
ever checked.

So the first question is not "why does R+X fail" but "is there a row for it at
all". Two shapes the defect could have, and they need different fixes:

1. The port's preset has no rows for the four power actions (and see #86 --
   healing has no binding either, which points the same way). Then nothing is
   bound and the game is behaving correctly.
2. The rows exist but a MODIFIER-plus-button binding is not a single physical
   code, so a preset that only ever writes one code per row cannot express it.
   The game's own PC editor binds one key per action, so how XMen2.exe encodes
   a two-input power select is an RE question that has not been asked here.

## What the next session should do first

Do NOT drive the game to find out. `tools/x2ctl.py input` reports the binding
rows and the per-player physical array live, and `tools/binding_rows.py` is
where the row table is written down. Ask, in this order:

1. Enumerate the installed rows and name which game ACTIONS they cover. The
   answer "the power rows are absent" ends the investigation at step 1.
2. If they are present: press R+X on the live run through `x2ctl.py` and read
   whether either physical code arrives at all, and whether the game's own
   power-select code is reached. A count with a denominator, not a screenshot.
3. Read the Xbox build's own defaults for these four actions -- `tools/xbe_query.py`
   already recovered the Xbox binding table (C187/C188), and it is the authority
   on what R+face is supposed to map to.

Related: #86 (healing unbound) and #87 (prompts still name keyboard keys) are
probably the same missing-rows cause seen from three sides. If one investigation
resolves all three, resolve all three.
