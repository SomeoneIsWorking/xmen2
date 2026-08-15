---
id: 77
title: Characters render black because the ENGINE computes near-black lights, not because of the D3D8 layer
status: open
symptom: characters render black or very dark in gameplay while the environment looks correct; Cyclops dark with head reading as collapsed
tags: rendering,lighting,d3d8,engine,recomp
created: 2026-08-15
updated: 2026-08-15
---

## What the picture shows

Oracle compare (tools/shot_compare.py, scratch/screenshots/oracle_compare.png)
found a like-for-like scene -- the Cyclops dialogue. In the control the
standing character is fully coloured; in ours he is a dark silhouette with the
correct outline. The seated character renders correctly in BOTH.

## Where it is NOT

Every one of these was measured with its denominator, and each is dead:

- lighting bound: 1 of 107,800 lit draws is bounded black (X2_LIGHT_SURVEY)
- state blocks: 0 of 8,686 applies changed the light table or the material
- vertex blending: D3DRS_VERTEXBLEND is NEVER set, so real D3D8 ignores
  D3DTS_WORLDMATRIX(1..3) exactly as this backend does (C194 is a dead end for
  this symptom)
- FVF blend weights: 0 of 271,635 draws carry them (the position-as-flags decode
  bug was real, and latent)
- index range: 0 of 273,289 draws read outside their vertex buffer
- normals: all 380 lit draws of a gameplay frame carry unit-length normals
- textures: 139 measured, mean luma 81/255; the 6 black ones are not bound in
  the frame
- D3DRS_NORMALIZENORMALS: the dark models have world scale 1.00, and the
  lighting computes identically with and without normalising

## Where it IS

At draw time, 85,156 enabled-light reads were compared against what SetLight
last wrote for that index: 0 differ, 0 arrive black although a colour was
written, 0 were never set. The D3D8 light table is FAITHFUL.

Every draw of the frame has the same five lights enabled:

    #3 directional, diffuse luma 0.14
    #4 #5 #6 #7 point, diffuse exactly 0.00

So four of the five lights are black and the fifth is dim -- and that is what
the ENGINE handed over. The renderer is faithfully drawing a scene whose lights
the recompiled engine computed wrong. The draws that look correct are bright
because their material emissive is 1.00; anything that depends on actual
lighting is dark.

Note the end-of-run SetLight summary shows those same indices ending on real
colours (light[4] 0.840 0.840 1.000, light[5]/[6] 0.784 0.980 0.996), so the
values move over time -- the engine blacks them and re-sets them.

## Next

tools/oracle_probe.py exists for exactly this: sample the CONTROL's guest state
from outside (process_vm_readv) at the same fields, and diff the two number
streams. That says whether the control's engine computes the same lights for
the same scene. If it does not, the divergence is upstream in the recompiled
engine and this becomes an RE question rather than a rendering one.

See C199, I055, I056.
