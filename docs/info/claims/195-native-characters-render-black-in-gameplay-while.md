---
id: C195
kind: claim
status: holds
created: 2026-08-14
tags: graphics,lighting,native
---

## Claim

Native characters render black in gameplay while the level, HUD and menus are correct -- established like-for-like against the stock oracle in two different scenes, with identical silhouettes, so it is shading and not geometry. The cause is NOT the state-block path and NOT a light table that stays black: 0 of 8,686 ApplyStateBlock calls changed the light table or the material, and every light slot ends the run non-black carrying exactly the colours the level data holds (light[5] and light[6] = 0.784 0.980 0.996, which is igLightAttr slot 8 in tutorial1.IGB; light[4] = 0.840 0.840 1.000; light[7] = 0.615 0 0). What the picture is drawn WITH at the moment it looks black is still unmeasured -- the earlier reading came from the level's first lit frames, while it was still loading.

## Evidence

Native scratch/screenshots/lv.ppm.023 vs cached stock oracle 9b555db04417d402 shots/stock.png.6, and gp.ppm.001 vs stock.png.2. Per-index SetLight summary and the ApplyStateBlock light/material counters, both added to the shutdown report and printed with their denominators, scratch/logs/sl.log. Ground truth for the light colours read straight out of the shipped level with build/igb_dump -obj on the 20 igLightAttr objects of Maps/Act0/tutorial/tutorial1.IGB: slot 7 ambient, slot 8 diffuse, slot 9 specular.

## What would falsify it

a light dump taken in a frame that is photographed black which shows the lights non-black -- that would move the fault out of the lighting inputs entirely, to the shader or the per-draw material
