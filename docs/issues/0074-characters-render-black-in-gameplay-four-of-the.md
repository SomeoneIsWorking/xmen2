---
id: 74
title: Characters render black in gameplay: four of the five lights reach D3D8 with diffuse (0,0,0)
status: open
symptom: In gameplay every 3D character is a black silhouette while the level geometry, the HUD portraits, the selection rings and the menus are correct. Faint teal streaks are visible on the black figures. Reported by the user as 'black models'.
tags: pc,native,graphics,lighting,d3d8
created: 2026-08-14
updated: 2026-08-14
---

## Measured, like-for-like

Two matched pairs, same scene and camera, native against the cached stock
oracle (key 9b555db04417d402):

  native scratch/screenshots/lv.ppm.023  vs  stock.png.6   (four heroes on the platform)
  native scratch/screenshots/gp.ppm.001  vs  stock.png.2   (the Cyclops conversation)

Room, floor, walls, HUD portraits and the green rings match the control. Every
3D character is black in ours and fully lit and coloured in the control. The
silhouettes are IDENTICAL, so this is shading, not geometry.

## One layer down: the lights themselves

X2_LIGHT_DUMP=10 on a gated level frame (X2_SHOT_AFTER_FILE=act0/tutorial),
identical in all ten dumps -- scratch/logs/lt.log:

    5 light(s) enabled, ambient 0,0,0, colorvertex 1, has_normal 1
    material diffuse 1,1,1  ambient 1,1,1  emissive 0,0,0
    light 0 (D3D index 3) type 3 DIRECTIONAL diffuse 0.000 0.196 0.196
    light 1 (D3D index 4) type 1 POINT       diffuse 0.000 0.000 0.000
    light 2 (D3D index 5) type 1 POINT       diffuse 0.000 0.000 0.000
    light 3 (D3D index 6) type 1 POINT       diffuse 0.000 0.000 0.000
    light 4 (D3D index 7) type 1 POINT       diffuse 0.000 0.000 0.000

So it is NOT the material (white), NOT a missing normal (has_normal 1), NOT the
global ambient term and NOT the vertex-colour path. Four of five lights are
black and the fifth is a dim teal -- which is exactly the picture: black
characters with faint teal streaks.

The positions, directions and types read sensibly out of the same D3DLIGHT8
the diffuse is read from, so the struct layout is right and the values really
are zero. This puts the fault UPSTREAM of the D3D8 boundary, in the engine's
own light attribute -- consistent with issue #62's probe, which found
igLightAttr 0x053e6268 handing setLightDiffuse a diffuse of (0,0,0,1).

One oddity to explain while chasing it: every light's Range reads
18446744073709551616.0 (2^64, the float 0x5F800000). That may be the engine's
own 'infinite range', or it may be a misread field.

## Next step

Find what writes the light attribute's colour (igLightAttr +0x30) during level
load, and why it stays zero for the point lights. The engine hands D3D8 what
it holds; the question is who filled it.

## Not this

The state-block path was suspected and is not obviously at fault: only
D3DSBT_ALL is accepted (PIXELSTATE/VERTEXSTATE are refused by name), and
capture/apply of the whole state is what D3DSBT_ALL means.

### Note (2026-08-14)
## Correction: that light dump described the LOADING frames, not the black gameplay frame

The reading above -- four point lights black at every lit draw -- came from the
first ten qualifying draws after the scene gate opened, and the scene gate opens
when the game OPENS the level package, not when the level is up. All ten were in
the level's first lit frames, still loading. C193 is falsified; C195 replaces it.

Three measurements added since, each printed with its denominator:

1. Per-index SetLight summary. Every light slot ends the run NON-black, with
   exactly the colours the level data holds:
       light[4] 14131 calls, 5 black; LAST type 1 diffuse 0.840 0.840 1.000
       light[5] 14131 calls, 5 black; LAST type 1 diffuse 0.784 0.980 0.996
       light[6] 14131 calls, 5 black; LAST type 1 diffuse 0.784 0.980 0.996
       light[7] 14131 calls, 5 black; LAST type 1 diffuse 0.615 0.000 0.000
   0.784/0.980/0.996 is igLightAttr slot 8 read straight out of
   Maps/Act0/tutorial/tutorial1.IGB with build/igb_dump -obj. The colours
   survive the whole path from the asset to D3D8.

2. ApplyStateBlock is INNOCENT: 0 of 8,686 applies changed the light table or
   the material. The suspicion recorded above is dead.

3. The 20 igLightAttr objects in the tutorial carry real colours -- (0.784,
   0.980, 0.996), (0, 0.51, 0), (1, 0, 0) -- and the one directional (type
   enum 0) carries (0, 0.196, 0.196), which is exactly what the port
   delivered. So the field reader is right for both types.

## What is actually unmeasured

What the CHARACTER draws are drawn with in a frame that is photographed black.
The light dump can now aim there: X2_LIGHT_DUMP_SKIP=<n> skips the first n
qualifying draws and every dump prints its presented frame number, and each
kept screenshot prints its own frame number, so the two can be lined up.
