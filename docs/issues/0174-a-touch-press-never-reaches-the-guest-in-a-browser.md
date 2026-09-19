# 0174 — a touch press never reaches the guest in a browser

State items: S020 (platform-neutral touch play), S021 (web product)
Status: open. The same press reaches the guest natively (`tools/live_case.py
touch-pad`, 7/7) and in `tests/test_touch_runtime` (44 checks); only the
browser loses it.

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

## The open question

Whether the reading thread can see the virtual joystick state the setting
thread wrote at all. `dinput_pad_virtual_report_reader_view` reports, on the
first pad refresh after each press, what the reading thread sees -- joystick
button, gamepad button, and the pending flag -- and says so when it finds
nothing. Its first form only spoke when it found a button held and was
therefore silent for a whole run, which was its own defect and not evidence.

## Next measurement

One browser run carrying the reader view in its press-triggered form, and the
per-button wait. If the reader thread reports the button up while the setter
reported it down, the two threads disagree about the same SDL device and the
transport is the defect; if it reports the button down, the loss is between
the joystick layer and what the guest assembles into DIJOYSTATE2.
