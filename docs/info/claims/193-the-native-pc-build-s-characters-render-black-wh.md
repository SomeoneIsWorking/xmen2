---
id: C193
kind: claim
status: falsified
created: 2026-08-14
tags: graphics,lighting,native
falsified_on: 2026-08-14
---

## Claim

The native PC build's CHARACTERS render black while the level, HUD and menus render correctly, and the cause is measured one layer down: of the five lights enabled at every lit gameplay draw, FOUR point lights arrive at D3D8 with diffuse exactly (0,0,0) and the fifth, a directional, with (0.000,0.196,0.196). The material is white (diffuse 1,1,1, ambient 1,1,1), global ambient is 0, colorvertex is 1 and the vertices carry normals -- so the darkness is not the material, not a missing normal and not the ambient term. The dim teal directional is exactly what the picture shows: black characters with faint teal edge streaks.

## Evidence

Like-for-like frames, same scene and camera: native scratch/screenshots/lv.ppm.023 vs cached stock oracle 9b555db04417d402 shots/stock.png.6 (the four-hero platform), and native gp.ppm.001 vs stock.png.2 (the Cyclops conversation). Room geometry, HUD portraits and the green selection rings match; every 3D character is black in ours and fully lit in the control. X2_LIGHT_DUMP=10 over a gated level frame (X2_SHOT_AFTER_FILE=act0/tutorial, X2_LIGHT_DUMP_MIN=300) printed the same five lights in all ten dumps, scratch/logs/lt.log.

## What would falsify it

a stock capture showing those same four point lights black (which would make the darkness faithful), or a light dump in which the four point diffuse values are non-zero while characters still render black

## FALSIFIED 2026-08-14

The like-for-like half stands -- characters ARE black in ours and lit in the control -- but the CAUSE half was measured on the wrong frames and must not stand. X2_LIGHT_DUMP prints the first n qualifying draws, and the scene gate opens when the game OPENS the level package, so all ten dumps came from the level's first lit frames while it was still loading, not from the gameplay frames the screenshots show. Two later measurements contradict the reading: a per-index SetLight summary shows every point light ending the run NON-black and carrying exactly the colours in the level data (light[5] and light[6] = 0.784 0.980 0.996, which is igLightAttr slot 8 in tutorial1.IGB), and the state-block counter shows 0 of 8,686 ApplyStateBlock calls changed the light table or the material. The instrument gained X2_LIGHT_DUMP_SKIP and a presented-frame number so the next reading can name the frame it describes.

> Anything that cited this claim as proof must be re-checked. Grep the repo for it.
