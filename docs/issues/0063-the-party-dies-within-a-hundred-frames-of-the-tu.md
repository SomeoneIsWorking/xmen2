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
