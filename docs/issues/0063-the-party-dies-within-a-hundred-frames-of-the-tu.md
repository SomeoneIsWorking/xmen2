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
