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

### Note (2026-08-12)
## The stock control now exists, and the tooling to get it

tools/run_shim.sh gained X2_KEYS="<seconds>:<key>,..." -- scripted input for the
WINE path, in wall-clock seconds, delivered with xdotool. Until now the stock
control could only photograph whatever the intro reached on its own, which made
it useless for anything past the menu, and "settle it against stock" is this
project's rule for every rendering question. Every press is reported, and so is
pressing NOTHING, because a control run that silently failed to drive the game
looks exactly like one that did -- which is how the first attempt was caught:
it searched for a window named "x2", found none, and sent no keys at all.

Timing matters and cost a run: Escapes at 245-272s backed OUT of the difficulty
dialog and returned to the menu. Moving them to 320s+ let the game load.

## What the control shows

Stock, driven to the opening in-game scene (the red-lit chamber, Cyclops and a
seated figure with a dialogue box): the room is DIM but plainly lit -- walls,
floor panels, both characters and their colours all readable.

Luma, whole frame:

    native gameplay   mean  2.3   frac<16 0.97   frac<32 1.00   frac>128 0.000
    stock  in-game    mean 28.8   frac<16 0.24   frac<32 0.78   frac>128 0.019

## What this does and does NOT establish

It does NOT establish a like-for-like difference: the two frames are DIFFERENT
MOMENTS in the level. The native shot is a facility corridor with pickups; the
control is the opening dialogue room. Do not quote "12x darker" as if it were a
measurement of the same scene.

What it does establish is scene-independent: the native gameplay frame has
ZERO pixels above luma 128 anywhere in 480,000, and 97% below 16. A dim room
still has highlights -- the control has 1.9% above 128. A frame with no bright
pixel at all is geometry receiving essentially no light, not a dark room.

So issue #62 is now a real defect rather than a suspicion, but the exact
comparison is still owed: drive the NATIVE build to the same opening dialogue
room (it is reachable -- the scripted route already passes through it) and
compare the two frames directly.

## Also confirmed in passing

The stock menu at this point in its cycle is a STARFIELD NIGHT sky, and earlier
samples showed the sunset. The menu cycles time of day, so the native build's
blue-sky and sunset captures are both correct rather than two different bugs.

### Note (2026-08-12)
## The instrument was reporting on the MENU, and that invalidated three readings

A multi-agent review of the renderer found no defect in it and instead caught
this: light_dump gated on a single process-lifetime counter, and the MENU is
lit and submits thousands of lit draws before a level loads. So every dump
described the scene that is KNOWN CORRECT. The project's own "cap the boring
case, not the interesting one" rule, broken in its own code.

Three readings taken before the fix are RETRACTED:
* "the material is black at every lit draw"  -- those were menu draws, and the
  53 black ones were outliers among 4000.
* "3905 of 4000 lit draws have a white material" -- also menu draws.
* the world-matrix sample -- menu draws.

X2_LIGHT_DUMP now requires the CURRENT frame to have already submitted
X2_LIGHT_DUMP_MIN draws (default 300; a menu frame here submits ~230, a level
frame ~600) and prints the threshold with the first line, so a dump of the
wrong scene is visible as one.

## What the LEVEL draws actually carry

    5 light(s) enabled, ambient 0.000 0.000 0.000 (D3DRS_AMBIENT raw 0x00000000)
    colorvertex 1, has_normal 1
    material diffuse 1.000 1.000 1.000  ambient 1.000 1.000 1.000  emissive 0
    light 0 type 3 (DIRECTIONAL) diffuse 0.000 0.196 0.196  amb 0 0 0
    light 1 type 1 (POINT)       diffuse 0.000 0.000 0.000
    light 2 type 1 (POINT)       diffuse 0.000 0.000 0.000
    light 3 type 1 (POINT)       diffuse 0.000 0.000 0.000
    light 4 type 1 (POINT)       diffuse 0.000 0.000 0.000

So the ENGINE supplies almost no light here: global ambient is genuinely zero
in the register, four of the five enabled lights have a black diffuse, and the
fifth is a dim teal directional at 0.196. A white material multiplied by that
is a near-black surface -- which is exactly the picture. The shader is
computing what it was given.

D3DLIGHT8 unpacking was re-checked field by field against the real struct
(Type 0, Diffuse 1-4, Specular 5-8, Ambient 9-12, Position 13-15, Direction
16-18, Range 19, Falloff 20, Atten 21-23) and matches, so the black diffuses
are not a misread.

## Where that leaves the cause

The defect is now UPSTREAM of the renderer: either the guest's own light setup
is not running as it should in this port, or these frames are legitimately lit
that way and the photographed area is simply meant to be dark. Nothing measured
so far separates those.

The control run is still the thing that decides it, and the missing half is
unchanged: drive the NATIVE build to the same opening dialogue room the stock
control captured (docs: the red chamber with Cyclops), and compare THAT frame.
If the native version of that specific room is also lit by one 0.196 teal
light, the light setup is wrong; if the native build never reaches that room on
the current scripted route, the route is what needs fixing first.

Do NOT adjust the lighting maths. Everything measured says the shader is
faithful to its inputs.

### Note (2026-08-12)
## The D3D8 layer is FAITHFUL. Chain verified end to end.

Every link between the engine's call and the shader's input was measured, not
reasoned about, and each one is correct:

* D3DLIGHT8 LAYOUT is right. Dumped raw: type=1 then [1-4] diffuse 1,1,1,1,
  [5-8] specular 0,0,0,1, [9-12] ambient 0.6,0.6,0.6,1, [13-15] position,
  [16-18] direction 0,0,1, [19] range, [20] falloff, [21-23] atten 1,0,0,
  [24] theta, [25] phi 3.142. Position and direction land where they must and
  read plausibly, which is the only way to confirm a struct layout.
* range 1.845e19 is NOT a misread. It is what the engine passes -- its
  "infinite range" idiom -- and it was the thing that first made the layout
  look wrong.
* The table is big enough: D3D8_MAX_LIGHTS is 16 and 16 are set, so no
  SetLight is refused for its index, and each slot is the full 26 floats so
  nothing is truncated.
* STATE BLOCKS DO NOT CLOBBER IT. 2000 consecutive Apply calls, and the light
  table was unchanged in every one; the material was unchanged in the six
  checked earlier. Apply restores the whole state, which is correct for
  D3DSBT_ALL, and D3DSBT_PIXELSTATE/VERTEXSTATE are refused rather than
  approximated. BeginStateBlock/EndStateBlock are unimplemented and the engine
  never calls them.

## What the engine actually supplies at a level draw

    5 light(s) enabled, D3DRS_AMBIENT raw 0x00000000
    material diffuse 1,1,1  ambient 1,1,1  emissive 0
    light 0 (D3D index 3) type 3 DIRECTIONAL diffuse 0.000 0.196 0.196
    light 1 (D3D index 4) type 1 POINT       diffuse 0.000 0.000 0.000
    light 2 (D3D index 5) type 1 POINT       diffuse 0.000 0.000 0.000
    light 3 (D3D index 6) type 1 POINT       diffuse 0.000 0.000 0.000
    light 4 (D3D index 7) type 1 POINT       diffuse 0.000 0.000 0.000

Index 7 is the same slot that was earlier SET to diffuse 1,1,1,1 with ambient
0.6 -- so the engine set it white and later set it black. Nothing in this port
overwrote it: Apply was checked and does not, and there is no other writer.
The engine turns its dynamic lights off by ZEROING them rather than by
LightEnable, which is ordinary, and at this point in the level four of its five
enabled lights are off that way.

So the picture is dark because the GUEST asked for a dark picture. The renderer
is drawing what it was given.

## What that leaves, and what NOT to do

Do not adjust lighting maths, light unpacking, or the state mirror. All three
are now measured correct, and changing them would be fitting the code to a
screenshot.

Two explanations remain and nothing measured separates them:
 (a) the photographed area IS meant to be dark, and the control's red chamber
     is simply a different place;
 (b) the guest's own light SELECTION is wrong in this port -- it is choosing
     the wrong set of lights, or running the code that fills them at the wrong
     time -- which is a guest-side question, not a renderer one.

The discriminator is unchanged and is a control run, not a code change: drive
the NATIVE build to the same opening dialogue room the stock control captured
and compare those two frames. If the native version of THAT room is lit by one
0.196 teal directional, it is (b) and the hunt moves into the guest.

### Note (2026-08-12)
## The control is now REPEATABLE, and the like-for-like is still not achieved

tools/run_shim.sh X2_KEYS gained a repeat window: "<from>-<to>/<step>:<key>"
fires a key every <step> seconds across a range. Exact instants proved too
brittle -- the six intro movies take a different wall-clock time every run, and
THREE control runs were lost to it: one where the press landed before the menu
appeared, and two where Escape opened the in-game PAUSE menu and the following
Return selected Quit, putting the run back at the main menu. A repeat window
blankets the uncertainty, and "195-300/12:Return,380-500/20:Return" now reaches
the opening red chamber reliably.

## What the control shows there

The red chamber, Cyclops and a seated figure, dialogue box up. Dim but plainly
lit -- walls, floor panels, both characters and their colours readable:

    stock red chamber   mean luma 27.2   frac<16 0.28   frac>128 0.019

## What is still NOT matched

The native build does not reach that room. With the cutscene skipped it lands
in a corridor with item pickups; WITHOUT any Escape it never leaves the
difficulty dialog inside a 4,600-frame budget, and when it does load, the first
gameplay frame photographed is the same corridor. Whatever the script, the
opening conversation does not appear in a native run. That is itself a fact
worth having and it is NOT explained: it may be that the conversation is
triggered by something that does not run here.

So the frames compared are still different places:

    native corridor     mean luma  2.3   frac<16 0.97   frac>128 0.000
    stock red chamber   mean luma 27.2   frac<16 0.28   frac>128 0.019

The scene-independent part stands and is the whole of the evidence: ZERO pixels
above luma 128 in 480,000. Every lit surface in the native frame is dark, and
the stock frame of a room described in the same words -- dim, enclosed,
artificially lit -- has 1.9% of its pixels bright.

## The next question is now a different one

Given the D3D8 layer is measured faithful (previous note), and given the native
build never reaches the opening conversation, the two threads may be the same
thread: something the level's script does at entry is not running. That would
explain both the missing conversation AND lights that were set white and then
zeroed with nothing turning them back on.

That is a GUEST-side investigation and it should start from the missing
conversation, which is a cleaner signal than a brightness number.

### Note (2026-08-12)
## The native run never leaves the MENU -- measured, not inferred

X2_FILES was rebuilt after it was caught lying (I046: 53 operations traced in a
run whose own counter reported 939 opens over 368 names -- it watched the
CreateFile family while the CRT's fopen went past it). With the corrected
instrument, a 4,600-frame native run opens 359 distinct names, and what they
are settles where the run gets to:

    packages/generated/maps/package/menus/legal_pc.pkgb
    scripts/menus/intro_normal.py          <- the script system RUNS
    packages/generated/maps/menu/main_back.pkgb
    maps/menu/main_back.igb                <- the MENU's backdrop

and NOTHING under maps/act1 or Conversations/. So the earlier reading -- "the
opening conversation never fires, and that may be the same defect as the dark
lights" -- was built on the broken instrument and is WITHDRAWN. The script
system is alive; the run simply never starts a game.

The four scripted Returns (f2639, f2815, f2830, f2900) do not carry it from the
main menu into a level in this run, though an earlier run did reach a corridor
around frame 3237. So the route is unreliable, not absent, and THAT is what
blocks the like-for-like comparison -- not anything about lighting.

## What the opening conversation actually is, for when the route works

Scripts/act1/codes_convo_start.py, in plain text, is three lines:

    # Generated by BehavEd
    # ( "starts conversation about security codes" )
    startConversation("act1/1_SOMETIMEACT1_0010" )

and Conversations/act1/1_sometimeact1_0010.{engb,XMLB} is the data. So the room
the stock control photographs is reached by a script call that this port's
script system is in principle able to make -- it is already executing
scripts/menus/intro_normal.py. Whether it makes it can be answered by the same
file trace the moment a run gets that far: the conversation file open is the
positive, and its absence with the level loaded is the negative.

## Next

Fix the route. The gate should be "maps/act1... was opened", which the file
trace can now assert, rather than a frame number or a draw count -- both of
which have already picked the wrong scene once each.
