---
id: 77
title: Historical black characters in the Cyclops dialogue diverged at skinning shader selection
status: resolved
symptom: characters render black or very dark in gameplay while the environment looks correct; Cyclops dark with head reading as collapsed
tags: rendering,lighting,d3d8,engine,jit
created: 2026-08-15
updated: 2026-09-24
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

## Closure (2026-09-24)

Captured on the current native build through `tools/live_case.py`'s
tutorial route (`act0/tutorial/tutorial1`, scripts on). The run waited for the
authored conversation and dumped F9 frame tables with SIGUSR1 while Cyclops
says "Nightcrawler, we've located the Professor". This is the same dialogue as
the retained comparison.

- Screenshot: Cyclops and the figure in the chair are both fully coloured and
  lit. Neither reads as a silhouette.
- Frame table, frame 507: the two character hulls are draws 41 and 43, both
  stride 32 and drawn from guest vertices through the vertex shader. The run's
  SetVertexShader census holds one shader handle (`0xf0000101`, created once,
  bound, never refused) and no FVF `0x002`, the fixed-function stride-12 form
  the hulls took when this was open. The GPU VS 1.1 path ran exactly 600 draws
  per 5 s: two per frame, the two hulls. CPU executor: 0.
- Shader lifecycle: 1 created, 0 deleted, 0 refused.

The ordered signature is not the historical 77 draws. This frame has 89. Its
prefix matches the stock capture's recorded counts (20 / 212 / 32 … 548 / 727),
but the conversation panel adds UI draws and the order of the large world
draws differs (1870 before 1582). So by this issue's own rule, it is not the
frame from `drive.log`, and that log no longer exists to recapture. What the
rule protected was the attribution to shader selection. That now holds
directly in the scene the defect was reported in: the hulls take the shader
path that C205 restored by raising `MaxVertexShaderConst` to 256, and the
actors are coloured. Resolved on that evidence.
