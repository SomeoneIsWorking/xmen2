---
id: 191
title: phone continue loaded a level without its heroes
status: resolved
symptom: Continue into Dead Zone showed the level with no party drawn and no minimap; the party moved and switched but stayed invisible until the player left the level and came back
state_items: S015
tags: save,autosave,continue,scripts,user-report
created: 2026-09-26
updated: 2026-09-26
---

# 0191 — phone continue loaded a level without its heroes

State items: S015 (transactional autosave and Continue)

REPORTED BY THE USER, 2026-09-26, on the phone with `boot.mode=continue`: "The
level and the characters didn't load", "Looks like a 'Continue' issue", "It
fixes itself if I go back to the previous level and go back again."

## Cause

The port's autosave (#99) fired 64 input polls after a successful map load
with the save manager idle. Dead Zone's opening script
(`Scripts/act1/deadzone/deadzone1/deadzone1.py`) runs in exactly that window:

```
lockControls(-1)
moveHeroesToEnt("outoftheway")
setGameFlag("deadzone", 2, 1)
... Blink is dragged away ...
moveHeroesToEnt("intheway")
lockControls(0.100)
```

The snapshot captured the party at `outoftheway` with the flag already set.
Continue restored both: the heroes stand where the camera follows them but
nothing is drawn, and the script that would move them back never runs again.
Travelling out and back loads the level fresh, which is why that repaired it.

It was not an ARM64 or v0.2.15 regression: v0.2.10, v0.2.14 and v0.2.16 all
show the same empty scene from the same save on desktop, through both boot
Continue and the menu's Continue.

## Fix

The autosave checkpoint also requires that the player controls a character
(`gameplay_control`: the retail HUD drew recently and no cinematic holds the
control lock). Any poll without control restarts the 64-poll count.
`x2_autosave_policy_poll` takes that fact; `/save` reports it as `control=`
and `control-deferred=`.

## Evidence

- `test_autosave_policy`: a locked party defers indefinitely; a lock one poll
  short of the checkpoint restarts the count.
- Live, desktop: `X2_BOOT_MAP=act1/deadzone/deadzone1` on a fresh profile held
  the checkpoint through the intro and the opening conversation
  (`control-deferred=61348`), wrote `autosave.save` once the party was playable,
  and `boot.mode=continue` from that file shows the whole party at the
  entrance.
- Negative: the older save written by the previous build (`scratch/perf`
  profile, same boot route) still restores the invisible party.

An autosave written before this fix stays wrong; leaving the level and
returning writes a correct one.
