---
id: 62
title: Gameplay renders almost entirely black -- cause not established, and a control is needed before touching lighting
status: open
symptom: The first level's facility interior is close to black in the native --d3d8 build: geometry present, a lit doorway and glowing pickups visible, floors and walls unlit. Menu renders correctly.
tags: pc,native,graphics,d3d8,lighting
created: 2026-08-12
updated: 2026-08-12
---

## What is seen

A scripted run into the first level (menu, cutscene skipped, level load)
photographed at frame 3236 shows the facility interior almost entirely BLACK.
The geometry is there -- a lit doorway at the top, a faint red chamber, two
glowing question-mark pickups -- but the floors, walls and props are close to
unlit. scratch/screenshots/play.png.

## What is RULED OUT, with the measurement for each

These were checked before forming any theory, because the last time a picture
was wrong here the stored explanation was wrong twice over.

* NOT the fixed-function lighting change of this session (lit-with-no-normal
  now takes emissive+ambient instead of white). The frame dump counts
  lit+nonorm 0, lit+norm 27, unlit 25 -- there is NOT ONE draw of the class
  that change touches.
* NOT a missing combiner stage: 0 draws enable a texture stage beyond stage 0.
* NOT dropped draws: 0 refused, of 134,906 submitted.
* NOT the 8-light limit. The state report says 16 lights SET, but the
  more-than-8-enabled message in d3d8_drawcall.c never fires, so no draw ever
  had a ninth enabled.
* NOT missing vertex colour. The level draws are stride 32 with col -1, and
  12+12+8 is exactly position, normal and one UV set -- the format HAS no
  diffuse, so there is no baked vertex lighting being dropped.
* NOT fog: FOGENABLE is 0.

So the surfaces are lit through the fixed-function pipeline with real normals,
from at most eight lights and a material, and the result is nearly black.

## What has NOT been established

Whether this is WRONG. This is an underground facility and some of it is meant
to be dark. There is no reference for this scene -- the one reference capture
in hand is of the main menu -- and this project settles rendering questions
against the stock Wine path, which has not been run for this scene.

Do not start changing light or material maths on the strength of a screenshot
that looks gloomy. Get the control first: the same scripted route under
./run.sh wine or tools/run_shim.sh, photographed at the same point, and compare.
If the control is equally dark there is nothing here.

### Note (2026-08-12)
## The stock control now exists, and the tooling to get it

tools/run_shim.sh gained X2_KEYS="<seconds>:<key>,..." -- scripted input for the
WINE path, in wall-clock seconds, delivered with xdotool. Until now the stock
control could only photograph whatever the intro reached on its own, which made
it useless for anything past the menu, and "settle it against stock" is this
project's rule for every rendering question. Every press is reported, and so is
pressing NOTHING, because a control run that silently failed to drive the game
looks exactly like one that did -- which is how the first attempt was caught:
it searched for a window named "x2", found none, and sent no keys at all.

Timing matters and cost a run: Escapes at 245-272s backed OUT of the difficulty
dialog and returned to the menu. Moving them to 320s+ let the game load.

## What the control shows

Stock, driven to the opening in-game scene (the red-lit chamber, Cyclops and a
seated figure with a dialogue box): the room is DIM but plainly lit -- walls,
floor panels, both characters and their colours all readable.

Luma, whole frame:

    native gameplay   mean  2.3   frac<16 0.97   frac<32 1.00   frac>128 0.000
    stock  in-game    mean 28.8   frac<16 0.24   frac<32 0.78   frac>128 0.019

## What this does and does NOT establish

It does NOT establish a like-for-like difference: the two frames are DIFFERENT
MOMENTS in the level. The native shot is a facility corridor with pickups; the
control is the opening dialogue room. Do not quote "12x darker" as if it were a
measurement of the same scene.

What it does establish is scene-independent: the native gameplay frame has
ZERO pixels above luma 128 anywhere in 480,000, and 97% below 16. A dim room
still has highlights -- the control has 1.9% above 128. A frame with no bright
pixel at all is geometry receiving essentially no light, not a dark room.

So issue #62 is now a real defect rather than a suspicion, but the exact
comparison is still owed: drive the NATIVE build to the same opening dialogue
room (it is reachable -- the scripted route already passes through it) and
compare the two frames directly.

## Also confirmed in passing

The stock menu at this point in its cycle is a STARFIELD NIGHT sky, and earlier
samples showed the sunset. The menu cycles time of day, so the native build's
blue-sky and sunset captures are both correct rather than two different bugs.
