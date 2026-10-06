---
id: C199
kind: claim
status: holds
created: 2026-08-15
tags: rendering,lighting,engine
---

## Claim

The black characters are NOT a D3D8-layer fault: the draw path sees exactly the lights the engine set, and the engine itself computes them nearly black

## Evidence

Measured at draw time over a gameplay run (scratch/logs/lc.log): 85156 enabled-light reads compared against what SetLight last wrote for that index -- 0 differ, 0 arrive black although a colour was written, 0 never set. Every draw of the frame has the same five lights enabled: #3 directional diffuse luma 0.14, #4-#7 point lights with diffuse exactly 0.00. The frame table's CPU replication of the shader gives the black character LIT 0.077-0.109 of 1 with best N.L 1.00, world scale 1.00, |N| 1.00, matdiff 1.00, emissive 0.00; the draws that come out bright differ ONLY in emissive 1.00. Ruled out with denominators beforehand: lighting bound (1 of 107800), state blocks (0 of 8686), vertex blend (D3DRS_VERTEXBLEND never set), FVF blend weights (0 of 271635), index range (0 of 273289), normals (380/380 unit), texture brightness (139 measured, mean luma 81, none of the black ones bound in the frame).

## What would falsify it

a measurement showing the control's engine computes the same near-black light values for the same scene -- which would mean the port's lights are faithful and the darkness comes from somewhere else entirely
