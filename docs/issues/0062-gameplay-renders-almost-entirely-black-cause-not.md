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
