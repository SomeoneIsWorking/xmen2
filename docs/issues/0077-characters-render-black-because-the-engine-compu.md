---
id: 77
title: Historical black characters in the Cyclops dialogue diverged at skinning shader selection
status: investigating
symptom: characters render black or very dark in gameplay while the environment looks correct; Cyclops dark with head reading as collapsed
tags: rendering,lighting,d3d8,engine,recomp
created: 2026-08-15
updated: 2026-08-24
---

## Observation

The retained oracle comparison shows the standing character as a dark
silhouette in the port and fully coloured in the stock game during the Cyclops
dialogue. Earlier run-wide and first-qualifying-draw lighting samples were not
bounded to that photographed frame. They found real black-light states, but
they did not establish that those states shaded the character in the picture.

## Scene-bounded correction

`scratch/logs/drive.log` frames 555 and 730 are the exact dialogue frame: 77
draws in the same order and with the same primitive counts as the retained
stock F9 capture in C203. At that boundary:

- Port character draws 28 and 30 are lit by non-black slots 8--11 and 12--19.
- The only divergent hulls are draws 29 and 31. In the port they are unlit,
  untextured FVF `0x002` draws at stride 12.
- The control renders those same two draw signatures with vertex-shader handle
  `0x003` at stride 32.

Near-black engine lights therefore do not explain this scene. The first exact
divergence is shader-path selection, upstream of the D3D8 draw implementation.
The broader eliminations still stand: state blocks changed no light/material
state in 0 of 8,686 applies; 0 of 273,289 draws read outside their vertex
stream; and all 380 sampled lit draws carried unit normals.

## Current state and closure discriminator

C205 established the likely correction mechanically: changing
`MaxVertexShaderConst` from the port's 96 to the control's 256 made the engine
create and bind its skinning shader 1,832 times instead of never. A 2026-08-21
capture of the current build (`scratch/screenshots/light-current-red-room2.png`)
shows both characters coloured and visibly lit, so the symptom no longer
reproduces.

The issue remains investigating because those two facts have not yet been tied
together in one scene-bounded current capture. Closure requires a current F9
frame table and screenshot of the same ordered 77-draw signature. Draws 29 and
31 must be stride-32 shader-handle draws, the shader-refusal count must be zero,
and the actor pixels must be coloured. A capture with any different ordered
primitive signature is a different scene and must be refused.

If a literal per-draw light comparison is still wanted, the missing side is the
stock proxy: the port frame table already records active light indices and
luma per selected-frame draw, while the proxy does not snapshot active light
state at each draw. Whole-run `X2_LIGHTLOG` has no scene/frame boundary, so its
nine route-dependent differences cannot answer this issue. See C203--C205,
I055 and I059. Do not conflate this historical dialogue defect with the
intermittent soldier-buffer defect resolved in issue #84.
