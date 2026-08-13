---
id: 62
title: Gameplay renders almost entirely black -- cause not established, and a control is needed before touching lighting
status: open
symptom: The first level's facility interior is close to black in the native --d3d8 build: geometry present, a lit doorway and glowing pickups visible, floors and walls unlit. Menu renders correctly.
tags: pc,native,graphics,d3d8,lighting
created: 2026-08-12
updated: 2026-08-13
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

### Note (2026-08-12)
## The two builds were never in the same LEVEL, and now that is measured

With the repaired file trace and the new repeat window on X2_INPUT_SCRIPT
(f2400-6000/60+20:Return), a native run loads:

    packages/generated/maps/act0/tutorial/tutorial1.pkgb
    maps/act0/tutorial/tutorial1.igb
    scripts/act0/tutorial/tutorial1/*.py     (36 of them, all found)

That is the TUTORIAL. The stock control reaches the act1 red chamber. Every
brightness comparison in this issue so far has been between two different
rooms, and the "dark corridor with item pickups" in the native screenshots is
the tutorial level, not a failed act1.

## The menus do not respond to the same driving

Driving the stock control the way the native run is driven -- Return every 6 s
from 150 s to 420 s -- leaves it on the MAIN MENU for all six samples. Sparse
presses (195-300/12 then 380-500/20) reach act1. So the same key pattern takes
the two builds to different places, and a like-for-like frame needs the ROUTE
matched, not just the timing.

Worth noting from those samples on their own: the stock main menu is fully lit
and detailed -- sunset sky, statues, torches, the animated backdrop.

## What the tutorial's own scripts say about lighting

    scripts/act0/tutorial/tutorial1/partylight_on.PY
        setPartyLightColor(" 0.840 0.840 1.000 ", 3.000 )
    scripts/act0/tutorial/tutorial1/partylight_red.PY
        setPartyLightColor(" 1.000 0.000 0.000 ", 3.000 )

The level's lighting is driven by script calls. The port sees five lights of
which four are black and one is a dim teal. Whether setPartyLightColor is ever
EXECUTED in this port is now a question a run can answer -- the scripts are
loaded (all 36 open successfully), which is not the same as being run.

## Instruments repaired this round, both of which had been reporting silence

- X2_FILES traced 53 of 939 opens (I046). Rebuilt; it is what found the level.
- X2_SHOT with a real window wrote nothing and said nothing, so a run launched
  without --no-window looked exactly like a scene gate that never opened. It
  now says so by name.

### Note (2026-08-12)
## The scene gate caught what was actually stopping the run: a SAVE DIALOG

The first capture taken with X2_SHOT_AFTER_FILE=act0/tutorial is not a dark
corridor. It is the tutorial level with a dialog over it:

    "No X-men Legends 2 save data present on hard disk."
    [Esc] Cancel        [Enter] Retry

with Nightcrawler's portrait in the corner and the red tutorial room behind.
The trace says why:

    FindFirstFile "S:\Activision\X-Men Legends 2\Save\saveslot*.save"
      -> "scratch/saves//Activision/X-Men Legends 2/Save"  matched NOTHING

There are no save slots, so the game asks, and every scripted Return was
pressing RETRY. The run was stuck in a modal loop, which is why a 9,000-frame
budget never got anywhere and why the earlier screenshots were of whatever was
behind it.

That frame also settles a measurement: it is NOT near-black.

    native tutorial (dialog up)   mean 29.8   frac<16 0.342   frac>128 0.035
    stock  act1 red chamber       mean 24.6   frac<16 0.422   frac>128 0.019

The port's own UI is brightly and correctly drawn here, and the room behind it
reads as a lit red interior. The "everything is black" figure came from frames
photographed with no gate at all.

## What this does NOT yet settle

The dialog covers most of the frame, so these numbers are mostly UI, not level
geometry. The comparison still needs a frame of the level with nothing over it,
which is what pressing Escape rather than Return should give.

## Two questions this opens

1. Is the dialog correct behaviour? A fresh install with no saves would show it
   on a real machine too. If so, the port needs no fix here -- but the driving
   script does, and so does anyone trying to play: Escape, not Enter.
2. Is scratch/saves//Activision/... (note the doubled slash) the path the port
   means to use? It resolves, but it is worth a look.

### Note (2026-08-12)
## The port PLAYS the tutorial. The party dies in it.

Driving Escape instead of Return past the save dialog gives the next screen:

    "ALL X-MEN HAVE BEEN ELIMINATED."
     Load Game
     Main Menu

So the sequence was never a renderer stuck on a black frame. It was: New Game
-> the act0 tutorial loads -> the level runs with nobody driving the character
-> Nightcrawler is killed -> the game offers to load a save -> there is no save
-> "No save data present on hard disk", and every scripted Return pressed
RETRY on that.

Both gated frames measure the same and neither is dark:

    dialog frame     mean 29.8   frac<16 0.342   frac>128 0.035
    game-over frame  mean 29.2   frac<16 0.348   frac>128 0.034
    stock red room   mean 24.6   frac<16 0.422   frac>128 0.019

## What is still not measured

Both are UI over the level. The level itself has still never been photographed
without something on top of it, because X2_SHOT overwrites and the END of a run
is a game-over screen. X2_SHOT_KEEP=<n> now keeps the first n qualifying frames
as <path>.000 onward, so the entry into the level is kept rather than the exit
from it. That filmstrip is what settles whether level geometry is dark.

## The claim to retire

"Gameplay renders almost entirely black" was measured on ungated frames, in a
level nobody had identified, in a run that was sitting in a modal loop. Nothing
about it survives as stated. What remains open is narrower and worth keeping:
the five lights the engine supplies are four black and one dim teal, which has
not been explained, and the party-light scripts that would set a bright one
have not been shown to execute.

### Note (2026-08-12)
## THE LIKE-FOR-LIKE FRAME EXISTS. Same room, same line, same camera.

X2_SHOT_KEEP kept the entry into the level instead of the exit from it, and
frame .003 of the native filmstrip is the opening conversation:

    CYCLOPS: "Nightcrawler, we've located the Professor. It's okay to teleport in."

which is the exact frame the stock control photographed. The act0 tutorial's
opening room IS the red chamber -- the two builds were in the same place all
along and the earlier "different levels" reading, while true of the LEVEL FILES,
does not apply to this frame. The camera, the geometry, the dialog box and the
line of text match pixel-for-pixel in layout.

## What differs, measured on the upper half of the frame (the level, not the UI)

    native   mean RGB  37.4   6.7   5.9      -- a hard red cast
    stock    mean RGB  19.7  16.2  12.7      -- near-neutral, slightly green

and by eye:

  - The stock room is dark teal-grey with a tan floor. Ours is uniformly red.
  - Stock shows Professor X seated and Cyclops standing, both LIT and coloured.
    Ours draws both as BLACK SILHOUETTES against the red room -- the geometry
    is there, correctly placed, and receives no light.
  - The HUD is correct in both, including the lit Cyclops portrait, so the
    fault is in world lighting and not in the renderer generally.

## What this retires and what it sharpens

"Gameplay renders almost entirely black" is retired: the level draws, the
conversation fires, the geometry and camera are right. What is wrong is
narrower and now has a control frame to be measured against: WORLD GEOMETRY IS
LIT THE WRONG COLOUR AND CHARACTERS RECEIVE NO LIGHT AT ALL.

The red is not obviously the "dim teal directional" the light dump reported, so
the next measurement is the light state AT THIS FRAME -- which is finally
possible, because the scene gate can hold the dump until the room is on screen.
The instruction not to touch the lighting maths still stands: the maths was
measured correct, and a red cast with black characters is a question about
WHICH lights arrive, not about what is done with them.

## Separately: the party dies immediately

Frame .005 onward is "ALL X-MEN HAVE BEEN ELIMINATED" -- about a hundred frames
after the level loads, with nobody driving. That is its own defect and it is
what has been ending every run early.

### Note (2026-08-13)
## The light state IN THE RED CHAMBER, at last

The light dump now shares the scene gate with the screenshot, so this is the
lighting of the frame in the A/B above and not of the menu:

    5 light(s) enabled, ambient 0.000 0.000 0.000 (D3DRS_AMBIENT raw 0)
    colorvertex 1, has_normal 1
    material diffuse 1,1,1  ambient 1,1,1  emissive 0,0,0

    light 0 (D3D index 3) type 3 DIRECTIONAL diffuse 0.000 0.196 0.196
            dir -0.11 -0.99 0.11
    light 1 (D3D index 4) type 1 POINT diffuse 0.000 0.000 0.000
            pos   -3.0   80.0 -6259.9   atten2 0.00003781
    light 2 (D3D index 5) type 1 POINT diffuse 0.000 0.000 0.000
            pos -264.2  -26.1  -501.0   atten2 0.00007500
    light 3 (D3D index 6) type 1 POINT diffuse 0.000 0.000 0.000
            pos -289.5  -19.5  -739.6   atten2 0.00007500
    light 4 (D3D index 7) type 1 POINT diffuse 0.000 0.000 0.000
            pos   20.5   14.6   194.2   atten2 0.00005000

## What I checked before believing it, and what it ruled out

The range on every light reads 1.8446743e19, which looks like a struct read at
the wrong offset -- the obvious suspect for four black lights. It is not:
sqrt(FLT_MAX) is 1.8446743e19 and that is D3D8's own D3DLIGHT_RANGE_MAX. The
positions and the attenuations are also plainly sane and all different from one
another. THE D3DLIGHT8 LAYOUT IS CORRECT and is no longer a candidate.

## What is left, stated precisely

Four point lights arrive with real positions, real quadratic attenuation, and a
diffuse of exactly zero. A level author does not place four black lights. So
the colour is being lost somewhere UPSTREAM of SetLight -- in the engine's own
light objects, in this port -- and the port's D3D8 layer is faithfully passing
on what it is given.

That also explains the picture. With every point light black, an ambient of
zero and a material emissive of zero, the only illumination reaching geometry
is a dim teal directional; the red the room is drenched in must be coming from
somewhere else -- colorvertex is 1, so baked vertex colour is the candidate --
and characters, which have no useful vertex colour, get nothing at all and go
BLACK. That is exactly the pair of symptoms in the A/B frame.

## Next, and it is RE rather than measurement

Find who calls SetLight with a black diffuse. The boundary ring already records
the guest return address of every crossing, so grouping SetLight calls by
caller names the engine function; from there the colour can be followed back to
the light object it came from. The instruction not to touch the lighting maths
still holds -- nothing downstream of SetLight is implicated by this dump.

### Note (2026-08-13)
## Black lights are RARE, and that changes what the A/B frame means

A histogram of every SetLight call, grouped by the guest return address:

    d3d8 SetLight: 130738 call(s), 151 of them with a BLACK diffuse,
                   from 6 distinct call site(s)
      0x1003d42e  28185 calls,   5 black      (all six sites are in libIGGfx,
      0x1003d4de  28185 calls,   4 black       within 0x1003d42e..0x1003d9a8 --
      0x1003d58e  28185 calls,   5 black       one applier, several call sites)
      0x1003d675  10324 calls,   4 black
      0x1003d75e  26498 calls, 129 black
      0x1003d9a8   9361 calls,   4 black

151 of 130,738 is one call in 866. The engine sets its lights EVERY FRAME, so
if four lights were black for the life of the level the count would be in the
tens of thousands. It is not. The black ones are set in a handful of frames.

The frame the light dump described -- and the frame in the A/B image -- is a few
frames after the level loaded. So what that dump caught may be the light table
DURING POPULATION rather than the lighting the room is meant to have, and the
red-with-black-characters picture may be a transient that a later frame would
not show.

That is a real weakening of the previous note and it is stated rather than
quietly dropped: "four black point lights" is still exactly what was measured,
but "the level's lights are black" no longer follows from it.

## Why a later frame cannot be photographed yet

The party dies about a hundred frames after the level loads (issue #63), so
there IS no later frame of the room to compare. #63 is now on the critical path
for this issue, not a side observation.

## What is still solid

  - The like-for-like frame exists and the two builds draw the same geometry
    from the same camera with the same dialog.
  - The D3DLIGHT8 layout is correct (the 1.8446743e19 range is D3D8's own
    D3DLIGHT_RANGE_MAX, sqrt(FLT_MAX)).
  - Cube sampling is implemented and is not why characters are black.
  - Whatever sets a black light does it from libIGGfx's own light applier, so
    the port's D3D8 layer is passing on what the engine computed.

### Note (2026-08-13)
## The colour cast holds against BOTH stock moments

The room's lighting changes as the scene plays -- the stock frame captured
early is grey-green and the one 400 s later is redder -- so a colour comparison
against one of them alone would have been a comparison of two different
moments. Against both, measured on the upper half of the frame (level, not UI):

    native  .003      mean RGB  37.4   6.7   5.9      G,B are 18%, 16% of R
    stock   early     mean RGB  19.7  16.2  12.7      G,B are 82%, 64% of R
    stock   late      mean RGB  30.2  17.3  14.2      G,B are 57%, 47% of R

The port's green and blue are a sixth of its red; the shipped game's are half
to four fifths of it. The cast is real and is not an artefact of which moment
was photographed.

Also from those late samples: Cyclops is lit and coloured in the stock frame
400 s in, exactly as he is at the start. Ours is a black silhouette. And issue
#63 is now confirmed a PORT DEFECT -- the stock party does not die -- so the
later frames this issue needs are reachable once #63 is fixed.
