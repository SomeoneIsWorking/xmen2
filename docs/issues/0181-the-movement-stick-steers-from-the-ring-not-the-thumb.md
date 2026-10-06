---
id: 181
title: the movement stick steers from where the ring is drawn, not from where the thumb landed
status: resolved
symptom: on a phone the on-screen stick "doesn't work right" -- the character walks off the moment the screen is touched, and full deflection is a short push one way against a long reach the other
state_items: S018,S020
tags: touch,input,stick,android,user-report
created: 2026-09-22
updated: 2026-09-22
---

# 0181 — the movement stick steers from the ring, not the thumb

State items: S018 (Android APK), S020 (platform-neutral touch play)

REPORTED BY THE USER, 2026-09-22, from their own phone: "touch stick doesn't
work right".

## What it was

A thumb does not arrive on the middle of a circle it cannot see. The stick
measured its axes from the ring's geometric centre, so that landing offset
*was* the player's input: the character walked off in whatever direction the
thumb happened to land, before it had moved at all, and the travel was
lopsided by the same amount — a short push towards the near edge against a
long reach to the far one.

Measured in the running product, with a thumb landing where a thumb lands
(0.45 of a radius right, 0.5 down):

| | landing, thumb not moved | one radius of travel up | one radius left |
|---|---|---|---|
| from the ring | `0.431, 0.479` | `0.431, -0.479` | `-0.533, 0.485` |
| from the thumb | `0.000, 0.000` | `0.000, -1.000` | `-1.000, 0.000` |

Two more things were wrong in the same arithmetic. Each axis was divided by
its own radius and clamped on its own, so a contact at the corner of the
bounding box reached 1.0 in *both* axes and the diagonals outran every other
direction. And there was no dead zone at all, so a hand's tremor holding the
phone steered the character.

The camera swipe drawn beside it already measured from `event.origin`, the
point at which the contact captured its zone; the stick did not.

## What it is now

`src/input/thumb_stick.{h,cpp}` owns the policy: travel is measured from the
contact's own landing point, one ring radius is full deflection, the result is
clamped to the unit circle rather than per axis, and travel inside 8% of full
is the thumb resting rather than steering — with the remaining travel rescaled
so the dead zone costs range instead of adding a step.

The ring also shows it. The knob was drawn dead centre whatever the player
did, so the one control with a value rather than a state was the only one that
never displayed its value; it now sits where the thumb has pushed the stick,
its travel read back from the stylesheet's own knob size rather than a second
copy of it.

## Why the tests did not catch it

`tests/test_touch_controls.cpp` landed its contact exactly on the ring's
centre — the one landing that cannot tell a stick measured from the ring apart
from one measured from the thumb. It now lands where a thumb does, and fails
against the old arithmetic.

## The measurement

`tools/live_case.py stick-travel` drives it in the running game: boot into the
tutorial map with touch forced on, wait for the retail HUD's own decision to
put the overlay up, then land a contact off the ring's centre and read the
deflection back from the run. It passes 9 of 9, and against the old arithmetic
it fails three of those checks with the numbers in the table above.

The ring's rectangle and the live deflection come from the new
`GET /controls`, so the case never keeps its own copy of the layout: a case
that works out where the ring ought to be stops testing the control the player
touches. That route also names which of its four preconditions is missing when
the overlay is drawing nothing — an empty answer that cannot say whether touch
play is off, the game is in a cutscene, or the overlay published no zone sends
a reader to three different places at once. The first run of this case read
`gameplay controls: never-seen` and that is what explained it: the overlay
follows the retail HUD's own per-frame heartbeat, so it is not up the instant
a skip returns control.

## What this does not cover

The deflection is verified through the port's own publication, not through the
character's position in the world. A stick that publishes the right axis into
a pad the guest is not reading would still pass; that is what
`tools/live_case.py touch-pad` covers separately.
