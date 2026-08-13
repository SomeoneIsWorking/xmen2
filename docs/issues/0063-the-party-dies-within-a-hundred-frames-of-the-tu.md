---
id: 63
title: The party dies within a hundred frames of the tutorial loading
status: open
symptom: ALL X-MEN HAVE BEEN ELIMINATED appears about a hundred frames after the act0 tutorial loads, with nobody driving the character
tags: pc,native,gameplay,tutorial
created: 2026-08-13
updated: 2026-08-13
---

Every driven native run ends the same way: the act0 tutorial loads, the opening
conversation plays, and by the fifth kept frame -- roughly a hundred frames
after the level appears -- the screen is "ALL X-MEN HAVE BEEN ELIMINATED".
Nobody is driving the character, so the party is standing still while something
kills it.

Measured with X2_SHOT_KEEP=24 on a run driven only by Returns through the menu:

    frame .000   loading, nearly black
    .001 - .004  the red chamber, the Cyclops conversation, characters present
    .005 - .023  identical game-over screen, frozen

This is what has been ending every run early, and it is why every screenshot
taken with an overwriting X2_SHOT was of a dialog: the END of a run of this
game is a game-over screen. It also produced the save dialog that ate the
scripted Returns -- the game offers to load after the party dies, there is no
save, and Return is RETRY on "No save data present on hard disk".

It is not established whether this is a port defect or the tutorial
legitimately killing an idle party. The stock control, driven differently,
reaches the same room and stays in it long enough to be photographed several
times, which is evidence for a port defect but not proof: the two runs were
driven with different key patterns.

Next: drive the stock control into the tutorial with the same pattern and see
whether it dies too. If it does not, look at what damages the party -- the
tutorial's turret and guard scripts are all loaded, in
Scripts/act0/tutorial/tutorial1/.

See issue #62, whose measurements were all distorted by this.

### Note (2026-08-13)
Issue #62 now depends on this one. The lighting comparison needs a frame of the red chamber from well after the level has settled, and the party dies about a hundred frames in, so no such frame exists. A SetLight histogram over a whole run shows black lights are 151 of 130,738 calls -- they cluster at level entry -- which means the only frame that CAN currently be photographed may be showing the light table mid-population rather than the room's real lighting.

### Note (2026-08-13)
## CONFIRMED a port defect: the stock game does NOT die in that room

The control was driven into the tutorial and then left alone for 900 seconds,
sampled eight times. Samples 3 through 8 -- roughly 400 seconds of run, with no
input at all after the entry Returns -- are all the SAME room, on a later
conversation line:

    CYCLOPS: "Get him to the X-Jet, Nightcrawler. We'll meet you there."

with Cyclops standing lit and coloured, no game-over, no save dialog. Mean luma
drifts 26.0 -> 27.6 across those samples, which is the scene animating, not a
screen change.

So an idle party does NOT die here in the shipped game. The native build's
"ALL X-MEN HAVE BEEN ELIMINATED" about a hundred frames in is this port's
defect, and the earlier "not established whether the tutorial legitimately
kills an idle party" is now settled against that.

Both runs entered the same way and were then left alone, so the comparison is
like-for-like on the one axis that matters.

### Note (2026-08-13)
## CORRECTION: the control was parked in DIALOGUE, not idling in gameplay

The previous note said "an idle party does NOT die here in the shipped game"
and called this a confirmed port defect. That claim is stronger than the
evidence. Look again at what those six samples actually show: all of them carry
the SAME line of dialogue --

    CYCLOPS: "Get him to the X-Jet, Nightcrawler. We'll meet you there."
    [Enter] continue...

-- so the control was sitting on a conversation box waiting for a keypress, for
the whole 400 seconds. Its Return window had ended at 500 s. It never entered
gameplay at all, and a party that is not in gameplay is not an idle party; it
is a party in a modal.

The native run, by contrast, was still being driven when it died: its Returns
ran to frame 4200, it advanced THROUGH the conversation, and the game-over came
within about a hundred frames of the last dialogue frame.

So the two runs were not compared on the axis I claimed. What is actually
established is only that the shipped game does not die while a conversation box
is up -- which nobody doubted.

The real test is running now: the control driven with Returns continuing to
960 s, so it leaves the conversation and reaches the same gameplay state the
native run reached. If it dies too, this is not a port defect and issue #62's
dependency on it disappears.

### Note (2026-08-13)
## WHEN it dies: at the end of the first conversation, within ten frames

A filmstrip at five-frame spacing (X2_SHOT_KEEP=40, X2_SHOT_EVERY=5) across the
whole event:

    .000-.003   loading, nearly black
    .004-.011   CYCLOPS "Nightcrawler, we've located the Professor..."
    .012-.013   NIGHTCRAWLER "Will do."          <- the last line
    .014-.039   "ALL X-MEN HAVE BEEN ELIMINATED", identical thereafter

Ten frames -- about a third of a second at the rate this run manages -- between
the closing line of the opening conversation and the game-over. No gameplay
frame exists between them: no enemy is on screen, no camera move, no fight.

That rules out the innocent explanation offered earlier. conv_0030a_end.PY does
spawn mercenaries and clear heroNoTarget, but that is the end of a LATER
conversation, and in any case a hero cannot be beaten to death in a third of a
second by enemies that have not appeared. Whatever ends the party here does it
at conversation end, not in combat.

Two shapes fit: the hero entity is not there (an empty party is eliminated by
definition the moment the check runs) or it is there with no health. The HUD
portrait through these frames is Nightcrawler's, so SOMETHING is in the party
slot.

### Note (2026-08-13)
## remove("simplecyclops") is REFUTED as the cause

The hypothesis was that the port's entity lookup resolves the conversation-end
script's `remove` to the wrong entity and takes the hero out of the party --
which would empty the party exactly when the death is observed. It is wrong.

Tested by substitution rather than by reading: X2_ASSETS was pointed at a copy
of Scripts/act0/tutorial/tutorial1/conv_0020b_end.py with its one `remove` line
commented out. The run reported the replacement by name --

    assets: REPLACED "scripts/act0/tutorial/tutorial1/conv_0020b_end.py"
            with scratch/noremove/.../conv_0020b_end.py (X2_ASSETS)

-- so the substitution definitely took, and the party still died: the filmstrip
goes "Will do." and then, by the frame after next, the save dialog that follows
a game-over.

This is worth keeping as a technique note as much as a result. X2_ASSETS makes
the level's SCRIPTS editable, so a hypothesis about what a script does to the
game can be tested by deleting the statement and running, without touching the
port or the RE. The next bisection is already running: the whole script emptied.
If the party still dies, nothing in conv_0020b_end causes it.

### Note (2026-08-13)
## ESTABLISHED, properly this time: the shipped game does NOT die, and it is IN GAMEPLAY

The earlier attempt at this comparison failed because the control was parked on
a conversation box. Driving it with the entry pattern that works plus a sparse
window to advance the dialogue --

    X2_KEYS="195-300/12:Return,380-500/20:Return,540-960/30:Return"

-- puts it in actual gameplay: an isometric view of the same room, three party
members standing with their selection rings, the full HUD with health bars and
a character portrait. Samples 4 through 8, about 350 seconds with no input
after 960 s, are all that state. Nobody dies, no game-over, no save dialog.

    stock, idle in gameplay   mean 33.4   frac<16 0.372   frac>128 0.032

So an unattended party does NOT die in the shipped game, in the state the
native run dies in. This IS a port defect, and this time the two runs are
compared on the axis that matters: both left alone, both past the conversation.

## That frame is also what issue #62 has been waiting for

It is the room in settled gameplay with nothing over it -- lit blue-grey, three
characters lit and coloured, health bars readable. It is the target the native
build has to be able to reach before #62's comparison can be finished, and it
is cached (oracle key 9b555db04417d402, sample .4 onward).

### Note (2026-08-13)
## Nothing in conv_0020b_end causes it either

The whole script replaced by two comment lines, the replacement reported by
name, and the party still dies. The substitution plainly took effect on other
things -- the HUD portrait changes from Nightcrawler to Cyclops and the
"Will do." line never appears -- so this is a real negative and not a
replacement that failed to fire.

So the cause is not in the conversation-end script at all.

## The next candidate, and why it fits the timing

tutorial1.py, the level's own entry script, opens with:

    remove ( "oz_explosion", "oz_explosion" )
    setRecallActive("FALSE" )
    ...
    lockControls(-1.000 )
    setallaiactive("FALSE" )
    cameraFocusToEntity("cam_prof", ...)
    startConversation("act0/tutorial/tutorial1/1_introlevel_0020" )

If `remove` resolves to the wrong entity HERE, the hero is gone from the moment
the level loads -- and the party check would not be expected to fire during a
cutscene with controls locked, which is exactly why the game-over appears the
instant the conversation ends rather than at load. The earlier refutation was
of the OTHER remove call, in the conversation script; it does not cover this
one.

Running now with that line commented out.

### Note (2026-08-13)
## The level script's remove() is refuted too

tutorial1.py replaced with a copy whose remove("oz_explosion") line is
commented out, replacement reported by name, and the party still dies. The
conversation ran longer this time -- more frames of "Will do." before the end
-- but the game-over follows the conversation's end exactly as before.

So both `remove` calls in this level are cleared, and so is the whole
conversation-end script. What survives every substitution so far is the
TIMING: whatever happens, the elimination lands as soon as the opening
conversation finishes.

## The bisection that separates scripts from the engine

Running now with tutorial1.py emptied completely. That script is what starts
the conversation, locks controls and disables AI, so with it gone the level
should simply sit there with the player free.

Either outcome is worth having:

  - Still eliminated -> no script in this level causes it, and the party is
    empty or dead independently of anything the level asks for. The next place
    to look is the party's own construction, before the level.
  - Not eliminated -> something in tutorial1.py is involved after all, and the
    remaining statements (setRecallActive, getOpened/actSilent on two doors,
    lockControls, setallaiactive, cameraFocusToEntity, startConversation) can
    be bisected one at a time.

Note what an immediate elimination would mean here: with no lockControls and
no cutscene, a party check has nothing to suppress it, so an empty party would
show up at once rather than at the end of a conversation that no longer exists.

### Note (2026-08-13)
## NO SCRIPT IN THE LEVEL CAUSES IT

tutorial1.py emptied to three comment lines, the replacement named in the
report. With it gone nothing starts the conversation, locks controls, sets a
camera or disables AI -- and the screen is BLACK for twenty-nine kept frames
(no camera was ever aimed) and then shows "ALL X-MEN HAVE BEEN ELIMINATED" on
that black.

So the elimination happens with no level script running at all. It is not
caused by anything the level asks for, and the three substitution experiments
are done: the conversation-end script's remove, the level script's remove, and
finally every statement of both.

## The anomaly that fits: THE HERO'S MODEL IS NEVER LOADED

Counting main-model opens in a full run's file trace:

    actors/11_professorx.igb        opened 2x
    actors/28_mystique.igb          opened 2x
    actors/77_mercenary.igb         opened 2x
    actors/128_civilian_male.igb    opened 2x
    actors/06_nightcrawler.igb      opened 0x     <-- the player character

Every other character in the level loads its main model. Nightcrawler's does
not -- and it is not a failed open either, since a failure would appear in the
trace as NOT FOUND. The game never ASKS for it. What it does load for him is
actors/06_nightcrawler_tail.igb and two animation sets (0603, 0610), so the
character is known and partly loaded; the body is what is missing.

An empty party is eliminated by definition the moment the check runs, and a
hero whose model was never requested is a good candidate for a hero that was
never constructed.

## What is NOT established, and must not be assumed next time

Whether the control's gameplay frame is even the same level. It shows THREE
party members and a helmeted portrait, and the tutorial gives you Nightcrawler
alone -- so the control had probably run on past the tutorial by then. That
does not weaken "the shipped game does not eliminate an idle party", but it
does mean the control frame is not yet proved to be the tutorial, and issue
#62 should not treat it as a matched frame until it is.

### Note (2026-08-13)
## CORRECTION, and the anomaly is sharper than the one I recorded

The previous note said "the hero's model is never loaded". That reading was
wrong. herostat.engb parses with tools/xmlb.py, and Nightcrawler's entry is:

    skin="0603"   characteranims="06_nightcrawler"   playable="true"

so actors/0603.igb is the BODY -- and the run opens it twice. What
06_nightcrawler.igb is, is the ANIMATION SET. The body loads fine.

The anomaly that survives the correction is narrower and better. Listing every
actor the run opens, each hero loads BOTH its skin and its animation set:

    Cyclops       0103.igb  +  01_cyclops.igb
    Wolverine     0303.igb  +  03_wolverine.igb
    Storm         0403.igb  +  04_storm.igb
    Magneto       2501/2504 +  25_magneto.igb
    Nightcrawler  0603.igb, 0610.igb, 06_nightcrawler_tail.igb -- and NOT
                  06_nightcrawler.igb

Every other hero's animation set is opened. The tutorial's player character is
the one whose is not, and it is not a failed open: a failure prints NOT FOUND
and none appears for anything nightcrawler-shaped. The file exists in the
install.

Four heroes loading also means a party WAS built -- Cyclops, Wolverine, Storm
and Magneto are the default team -- which makes "the party was never
constructed" the wrong shape. What fits is: the level's own player character
fails to come up, and the party the CHECK looks at is the level's, not the
menu's.

## The caveat this must carry

The file trace still does not cover CreateFileMappingA/MapViewOfFile, which is
a real mmap in this host and was named as a hole in issue #60 and never closed.
So "never opened" means "never opened through CreateFile or the CRT". If the
animation set is loaded through a mapping, it would be invisible here and the
whole anomaly evaporates. CLOSING THAT HOLE IS THE NEXT STEP, before any more
weight is put on this.

### Note (2026-08-13)
## The mmap caveat is RETIRED: "never opened" can be trusted

Issue #60 named CreateFileMappingA/MapViewOfFile as a hole in the file
instrument and it was never closed, so the previous note carried it as a reason
to doubt the anomaly. Reading the code settles it the other way:

  - CreateFileMappingA takes an already-open FILE HANDLE (its first argument),
    not a path. Anything mapped was opened by name first, and that open goes
    through CreateFileA, which is traced.
  - CreateFileW is not implemented at all and refuses by name.
  - The CRT has exactly one open by name, fopen, and it goes through the same
    resolver.
  - No run has hit an unimplemented import with a file-ish name; the report
    would print one.

So the only two ways this game can open a file by name are both traced, and
"actors/06_nightcrawler.igb is never opened" stands.

## The positive control now running

If the hero is never spawned, forcing the spawner should load his animation set
and stop the elimination. tutorial1.py is substituted with a copy that calls

    act ( "spwnr_nightcrawler", "spwnr_nightcrawler" )

directly -- which is what nightcrawler_spawn.PY does when its trigger fires --
and the run watches for actors/06_nightcrawler.igb appearing in the trace.

Designed so both outcomes say something. The anims load and the party survives:
the spawn trigger never firing is the defect, and the hunt moves to what fires
it. The anims still do not load: the spawner itself does not work here, which
is a different and more local defect. The anims load and the party still dies:
the hero is not what the party check is counting.
