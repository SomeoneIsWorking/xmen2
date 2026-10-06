---
id: C196
kind: claim
status: holds
created: 2026-08-14
tags: graphics,lighting,native
---

## Claim

In a REAL gameplay frame the lighting inputs are correct, which kills the black-light theory: at presented frame 5740 the five enabled lights carry (0,0.196,0.196) directional and (0.840,0.840,1.000), (0.784,0.980,0.996), (0.784,0.980,0.996), (0.300,0,0) point -- the level data's own colours -- with a white material and vertex normals present. The remaining discrepancy in the same dump is SPATIAL, not chromatic: the point lights sit within ~500 units of the origin (0,63.6,-300.5), (44.7,-325.5,-10.9) while the world matrix places the geometry at (-2314.8,-4088.1,3666.9). With quadratic attenuation of 3.78e-5 and no constant or linear term, a distance of ~5000 attenuates to about 1/750 -- black. Either the lights and the geometry are in different spaces, or the world matrix this draw is lit with is not the one the geometry is drawn with.

## Evidence

scratch/logs/lp.log, X2_LIGHT_DUMP=8 X2_LIGHT_DUMP_MIN=100 X2_LIGHT_DUMP_SKIP=2400 with the scene gate at act0/tutorial; the report line states 2408 draws qualified, 2400 skipped, 8 of 8 printed, and each dump carries its presented frame number. The same instrument at SKIP=0 dumps the level still LOADING and showed black point lights, which is what C193 wrongly generalised.

## What would falsify it

a dump of a draw known to be a CHARACTER (rather than any lit draw) showing the light positions and the world matrix in the same space -- the dumped draw's identity is not yet established, and only ~2 draws per level frame are lit at all
