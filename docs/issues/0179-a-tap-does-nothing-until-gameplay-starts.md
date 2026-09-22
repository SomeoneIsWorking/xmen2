---
id: 179
title: a tap does nothing until gameplay starts, so a phone cannot leave the title screen
status: resolved
symptom: on a phone the intro could not be skipped by tapping and the main menu did not respond to taps; every contact before gameplay was counted and discarded
state_items: S018,S020,S021
tags: touch,input,menu,fmv,mouse,pointer,android,web,user-report
created: 2026-09-22
updated: 2026-09-22
---

# 0179 — a tap does nothing until gameplay starts

State items: S018 (Android APK), S020 (platform-neutral touch play),
S021 (web product)

REPORTED BY THE USER, 2026-09-22, from a real session on a phone: "the touch
screen wasn't working, I couldn't skip intros via tapping and I couldn't
interact with the main menu."

Status: FIXED. A contact with no drawn control under it is now the retail
GUI's pointer. Measured by `tools/live_case.py menu-touch`, 9/9.

## The cause

`x2_touch_runtime_event` routed a contact only while
`x2_touch_runtime_overlay_visible()` was true, and that is

```
window && x2_touch_runtime_active() && x2_gameplay_control_active(now)
```

The gameplay gate is `never-seen` until the retail HUD has drawn, `hud-stale`
through a load, and `cutscene-locked` through a cutscene. So for the whole of
the legal splash, the intro movies, the main menu, the load and save screens
and every cutscene, every finger event was counted into
`ignored_overlay_hidden` and thrown away.

It had no second route either, and deliberately so: `x2native.c` sets
`SDL_HINT_TOUCH_MOUSE_EVENTS=0` before SDL creates its event sources, so that
an action-pad tap cannot also reach the retail world-click handler. Correct
for gameplay, and it meant a tap outside gameplay produced nothing at all.

A run confirms the gate, with no window and the setting forced on:

```
[touch] [HB] no contact reached the port this run -- touch_controls=ALWAYS,
        source says not touch, gate never-seen, a window was present
```

That line printed for every heartbeat of the boot, and `gate never-seen` is
the whole defect.

## Why no test and no browser run caught it

Every measurement of touch reached gameplay first, by keyboard.
`tools/web_touch_play.py` presses Escape and Enter while it waits for the gate
to reach `active` (`--no-skip` turns that off), and issue #171's browser
evidence was taken on the tutorial map. The one thing the harness did on the
player's behalf was the one thing the player could not do.

`tests/test_touch_runtime.cpp` did cover the hidden-overlay case — and
asserted the discard was *counted*, which was the wrong contract stated
confidently.

## The fix

Those screens are the retail GUI, and the retail GUI takes a mouse: issue #132
put `igWin32Window::getEvents` back in the loop, so `WM_MOUSEMOVE` and the
button messages reach `igMouse` and the interface manager. The port already
moves that pointer from a finger for one case — the portrait tap that selects
a hero — so a contact with no drawn control under it takes the same route, at
its own position.

Retail draws one cursor and has one button, so one contact owns it at a time;
that rule was inside `PortraitPointer` and is now `PointerOwner`, used by
both. A button pressed by a finger is always released by something: a lifted
finger, a lost window, or gameplay starting underneath it.

Nothing about the gameplay overlay changed, and nothing here branches on the
platform it was built for.

## Measured

`tools/live_case.py menu-touch`, each half against a control that must come
out the other way:

| check | result |
|---|---|
| a tap ends the intro movie | ended 6.5s in, tapped at 6.0s; untouched the same movie runs 10.0s |
| a tap on empty sky | opens nothing |
| a tap on the OPTIONS row | the game's own first open of `menus/options.pkgb` |
| the census | 6 contacts to the retail pointer, 0 dropped |

Timing is the measure for the movie, not the frame counts in its end summary:
the decoder runs ahead and the summary is printed at unload, so a skipped
movie still reports 312 decoded either way. Nor is a pixel delta the measure
for the menu — the menu animates, and its idle frame-to-frame difference
measured 11–19 against the 27 a working tap produced, so a threshold could
have been set to make either answer come out.

## What this needed that did not exist

A host with no touchscreen could not press its own screen. `/touch?x=&y=` on
the control channel drives a contact through the runtime's own injector —
which takes the same note-source and routing calls the host event pump takes,
so it exercises the shipping path and not a second copy of it. `x2ctl.py
touch 0.2,0.73` is the client.

## Still open

The retail footer still prompts `ESC BACK` and `SPACE ADVANCED OPTIONS` —
key names, to a player with no keyboard. In touch mode those should read
`BACK` and `ADVANCED OPTIONS` on a tappable control. Separate issue.
