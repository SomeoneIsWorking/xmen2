---
id: 174
title: a touch press never reaches the guest in a browser
status: resolved
symptom: the overlay's own virtual pad read as a player plugging in a controller, which flipped the input source away from touch and cancelled every press
state_items: S020,S021
tags: web,browser,touch,input,pad,dinput
created: 2026-09-19
updated: 2026-09-19
---

# 0174 — a touch press never reaches the guest in a browser

State items: S020 (platform-neutral touch play), S021 (web product)
Status: FIXED. The overlay's own virtual pad was flipping the input source
away from touch, which cancelled every held zone about a millisecond after the
press was made. Measured after the fix (`scratch/web/wasmgoal/verify18`): 30
of 31,840 guest reads came back DOWN and 14 axis reads off centre, against 0
of 167,890 before.

## Symptom

Four browser runs (`scratch/web/wasmgoal/verify11..14`), all with the overlay
drawn, contacts landing on it and player one claimed:

```
touch: first press of "b" published -- joystick button 1 set (joystick itself
reads DOWN); gamepad "b" (enum 1) now reads DOWN
touch: first press of "b" released -- the game read a button 0 time(s) while
it was held, 0 of them DOWN
  pad: the game read a button 39850 time(s); 0 of those came back DOWN, 0
  could not answer at all. Axes read 23910 time(s), 4 off centre.
VERDICT: a touch on a drawn control reaches the pad in this browser -- 48
contact(s), 62 zone action(s), 4 button change(s) and 32 axis change(s)
published.
```

So the overlay publishes, the setter's own read-back through the game's own
call says DOWN, and the guest's polls -- every one of which reaches a real
device with a real handle, since the empty-slot and no-handle counters both
stay at zero -- never see it.

## What has been ruled out

- **The pad is missing or in another slot.** `dinput_pad_button_read_count`
  and the empty-slot counter say every read reached the device.
- **The game never polls.** It polls about 420 times a second in game.
- **The press is released before a poll.** A deferred release now holds a
  press until the pressed button's own reader has seen it, bounded at 0.30 s,
  and the browser reports those deferrals happening.
- **A diagnostic satisfied the wait.** The probe's reads used to count as the
  game's; diagnostics now read through the uncounted entry points.
- **The wait was satisfied by another value's reader.** The wait is per button
  and per axis, not on a total.

## The cause

The on-screen controls publish through an SDL virtual joystick, and SDL
announces every button and axis they set as an ordinary joystick and gamepad
event. `x2_touch_source_note` read those as a controller arriving, flipped the
source away from touch, and `x2_touch_runtime_cancel` let go of every held
zone -- so the release arrived as a cancellation, took the press back instead
of completing it, and never waited for a reader.

The tell was in every run's own beat: `source says not touch` printed in the
same line as arriving contacts. The counter beside it was reported as
"cancellation(s) for a lost window, rotation or layout change" -- three causes
it had never observed, none of them this one.

The fix is the same one the `SDL_TOUCH_MOUSEID` checks already applied to the
synthetic mouse events a touchscreen produces: that pad is the same finger.
`x2::input::touch_pad` tells `touch_source` which joystick id is the port's
own, joystick axis events gained the deflection test gamepad ones already had,
and the four cancellation causes are counted apart.

## The question it took to get there

Whether the reading thread can see the virtual joystick state the setting
thread wrote at all. `dinput_pad_virtual_report_reader_view` reports, on the
first pad refresh after each press, what the reading thread sees -- joystick
button, gamepad button, and the pending flag -- and says so when it finds
nothing. Its first form only spoke when it found a button held and was
therefore silent for a whole run, which was its own defect and not evidence.

## What the measurements said, in order

The reader-side view reported, on the first pad refresh after each press, that
nothing was down and nothing was awaiting a reader -- on the same thread that
set it. The hold never printed, so the release's guard was refusing; its
negative then named the term: `taken back 1`. That is `wait_for_a_reader == 0`,
which only a cancelled contact produces, while the contact census counted zero
cancelled fingers. The cancellation was coming from the port itself.

## Still open

A press on "start" is still reported `taken back 1`. Opening the pause menu
hides the overlay, so that cancellation may be correct; the per-cause counts
now in the census will say which cause fires.
