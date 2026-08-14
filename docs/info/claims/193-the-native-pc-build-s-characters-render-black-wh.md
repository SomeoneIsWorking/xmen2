---
id: C193
kind: claim
status: holds
created: 2026-08-14
tags: graphics,lighting,native
---

## Claim

The native PC build's CHARACTERS render black while the level, HUD and menus render correctly, and the cause is measured one layer down: of the five lights enabled at every lit gameplay draw, FOUR point lights arrive at D3D8 with diffuse exactly (0,0,0) and the fifth, a directional, with (0.000,0.196,0.196). The material is white (diffuse 1,1,1, ambient 1,1,1), global ambient is 0, colorvertex is 1 and the vertices carry normals -- so the darkness is not the material, not a missing normal and not the ambient term. The dim teal directional is exactly what the picture shows: black characters with faint teal edge streaks.

## Evidence

Like-for-like frames, same scene and camera: native scratch/screenshots/lv.ppm.023 vs cached stock oracle 9b555db04417d402 shots/stock.png.6 (the four-hero platform), and native gp.ppm.001 vs stock.png.2 (the Cyclops conversation). Room geometry, HUD portraits and the green selection rings match; every 3D character is black in ours and fully lit in the control. X2_LIGHT_DUMP=10 over a gated level frame (X2_SHOT_AFTER_FILE=act0/tutorial, X2_LIGHT_DUMP_MIN=300) printed the same five lights in all ten dumps, scratch/logs/lt.log.

## What would falsify it

a stock capture showing those same four point lights black (which would make the darkness faithful), or a light dump in which the four point diffuse values are non-zero while characters still render black
