---
id: 180
title: a footer action prompt names a key a phone does not have
status: resolved
symptom: in touch play the retail footer still read "Esc Back" and "[Space] Advanced Options" -- key names, offered to a player with no keyboard and nothing to press
state_items: S020
tags: touch,input,menu,ui,text,user-report
created: 2026-09-22
updated: 2026-09-22
---

# 0180 — a footer action prompt names a key a phone does not have

State items: S020 (platform-neutral touch play)

REPORTED BY THE USER, 2026-09-22, continuing [#179](0179-a-tap-does-nothing-until-gameplay-starts.md):
"in touch mode, menu items like ESC BACK and SPACE ADVANCED OPTIONS should be
just BACK but in a touch button."

Every screen before gameplay offers its actions as a key and a word. The key
is drawn by the port's own composed cap, which is correct on a desktop and
useless on a phone: the player is told to press a key that is not there, and
the words beside it are not a control.

## What ships now

In touch play the key's glyphs are collapsed where the emitter writes them,
the action's words slide into the space they left, and the rectangle those
words landed in is published as a control. A contact inside it presses the
DirectInput code the prompt named — not a key name round-tripped through a
scancode table, the code itself, retained when `prompt_labels.c` composed the
cap and `FUN_006281f0` had just named the binding.

Nothing about the retail layout, font or wording changes, and the control
draws no art of its own: the words retail already drew ARE the button.
`/prompts` on the control channel lists what is pressable and in which
surface, so a run driving itself taps what the game is actually showing
rather than a coordinate chosen before it started.

## Three wrong answers, each caught by the running game

**The cap had to open the string.** It does in the difficulty dialog, and in
no menu footer in the game: those reach the glyph loop as
`<3ed><90><91><91><91><92><92><92>Esc<93> Back`, the authored text's token
marker still in front of the cap. 23,819 of 24,525 drawn strings went
unclaimed and the one screen the user named published nothing. The cap is now
searched for at any offset.

**The rectangle was placed by the transform of whichever draw finalized
next.** A frame lays every prompt out and only then draws them, one element
per non-indexed draw with its own world matrix, so emptying the queue into
the first finalizer put "Back" on top of "Advanced Options" — same measured
engine rectangle, 774,664 instead of 251,664. Taking one per draw in turn
instead moved the difficulty dialog's second prompt onto another element's
line. The draw's own primitive count is what attributes it: a glyph occupies
six vertices and the draw declares two fewer primitives than vertices, so the
draw of a 15-glyph "Back" declares 88 and the draw of a 32-glyph "Advanced
Options" declares 190.

**A re-measurement queued as a second prompt.** The footer is laid out far
more often than it is drawn — 754 of 796 retained prompts in one run were the
same prompt measured again — and a four-deep queue of them pushed out the
prompt still waiting for its draw. A re-measurement now replaces the one
retained. Evictions went from 726 in a run to 0.

## The measurement

`tools/live_case.py prompt-touch` drives it: reach the menu, open Options,
read `/prompts`, tap the Back control it names, and require the screen the
run is on afterwards to be a different one -- stated by which prompts it
draws, Options' own pair being Escape and Space. The census must also count
the press and report no refusal from the keyboard injector. It passes 9 of 9,
three runs in a row.

The obvious measures are both wrong here and both were tried. A pixel delta
cannot say the screen changed: this menu animates, and a run in which the tap
HAD worked measured 40.96 from the Options screen against 42.54 from the menu
it returned to, so a threshold could have been set to make either answer come
out. Nor can re-opening Options from a fixed row coordinate: the menu slides
its rows back in and they are not where they were.

The menu check is the negative one: the main menu draws no action prompt, so
nothing may be pressable there. That is what catches a control outliving the
screen that drew it — the difficulty dialog's own "Esc Back" is published
seconds earlier and stays pressable for two.

A run also says what it walked away from. A drawn string carrying a key cap
this owner did not claim prints once per distinct string, because "17,820
unmatched" cannot tell a footer nobody can press from the ordinary words that
also pass through the same loop.
