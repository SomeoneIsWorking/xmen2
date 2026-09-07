# Touch play

**Touch is a device, not a platform.** The on-screen pad and the mobile HUD
placement belong to whoever is touching a screen right now — a Windows or Linux
tablet, a 2-in-1 laptop, a phone, or a desktop player who reached out and
prodded the monitor — and they do not belong to an Android player holding a
controller. Nothing here is compiled out anywhere, and no `__ANDROID__` decides
any of it. `docs/android-release.md` owns the APK; it consumes this, it does not
own it.

This document owns the touch-play contract. `docs/codemap.md` places the files.

## What decides that touch is in use

`src/input/touch_source.c` classifies every host event into touch / not-touch
and nothing else, because exactly one decision depends on it: whether the pad
and the HUD placement that comes with it are on screen. It becomes yes at the
first contact and no again at the next real keyboard, mouse or controller
event.

Three details are the whole reason it is a separate owner, testable with no
window and no game:

- SDL reports touch as mouse motion as well. That synthetic pointer carries
  `SDL_TOUCH_MOUSEID` and is the same finger, so reading it as a mouse would
  put the pad away the instant it was used.
- A resting thumb or a pad drifting in a drawer is not the player picking up a
  controller: an axis must pass half of SDL's signed range to count.
- Events from every other device kind are ignored, not counted as not-touch. A
  controller being plugged in is not the player picking it up.

`src/config/settings.c` forces either end on every platform through
`input.touch_controls`: `OFF`, `AUTO` (observe the device — the default), or
`ALWAYS`. `ALWAYS` is what makes the layout reachable on a desktop with no
touchscreen at all; a layout nobody can look at until it is on a phone is a
layout that ships wrong.

`x2_touch_runtime_active()` is the one answer, read by both the overlay and the
HUD relocation. The HUD moving while no pad is drawn would be the HUD making
room for nothing.

## The layout and the action vocabulary

The title owns the safe-area-aware layout and action vocabulary in
`src/input/touch_controls.cpp`, while `src/input/touch_runtime.cpp` converts SDL
contacts to the existing virtual DirectInput pad through `lucent::touch::Router`.
Lucent owns capture, multi-touch, and cancellation, not the title's action
vocabulary. A contact stays with its zone after leaving the zone until it ends
or is canceled. The runtime derives safe-area insets from SDL and publishes
releases on cancellation, rotation, or lifecycle loss.

The landscape layout uses these zones and the existing Xbox-derived action
rows. Internal retail storage names are not player-facing labels: the touch
document uses the action meanings proven by `binding_rows.c` and
`xbox_defaults.c`.

| Zone | Action mapping |
|---|---|
| Left virtual stick | `Forward`, `Backward`, `MoveLeft`, `MoveRight` |
| Bottom-right action cluster | Light attack, heavy attack, use, jump, mutant powers, energy pack, health pack |
| Retail party portraits, top-right | Pointer press/release through the existing Win32 mouse-message path; the retail click handler selects the tapped hero |
| Physical D-pad | Next hero, previous hero, decrease aggression, increase aggression; these retail bindings remain valid |
| Menu buttons | `Pause`, `Stats` |
| Open playfield swipe | Relative camera movement from the contact's Lucent capture origin; no second visible stick |
| Retail health/energy HUD, top-left | The retained CHud draw path, relocated only while touch mode is active |

The layout must leave an inset for cutouts/navigation bars, support at least the
left stick plus two face/shoulder contacts simultaneously, expose a
reconfigure/hide-controls setting, and make touch feedback visible without
changing the input action delivered to the guest. The mapping is derived from
[`xbox_defaults.c`](../src/native/xbox_defaults.c), not invented per screen. The
shipped feedback document mirrors only authored touch controls with text labels,
rather than showing controller glyphs whose internal action names are
misleading. Gesture and portrait hit regions remain invisible, captured zones
highlight, and the persistent Input setting can hide the controls. Held contacts
persist until finger-up/cancel rather than expiring on a test-channel timeout.

## Reaching the guest at all

A pad only reaches the guest once a player resolves to it, and a player resolves
only from an explicit transient assignment or a persisted reservation. On a
touch-first first run neither exists, so every touch went into a pad no player
was reading: the probe reported the game polling buttons that were never down,
while SDL's touch-to-mouse emulation carried presses to menus and nothing to
gameplay. `claim_player_one()` fills that vacancy only — a controller the player
already chose keeps player one.

## What is verified, and by what

| Check | What it pins |
|---|---|
| `ctest -R touch_source` | The device classification, including the `SDL_TOUCH_MOUSEID` synthetic pointer and the resting-stick threshold |
| `ctest -R touch_controls` | Action vocabulary, zone routing, portrait pointer arbitration, cancellation on layout change |
| `ctest -R touch_layout` | Safe-area-aware zone placement |
| `ctest -R hud_layout` | The pure HUD edge-relocation policy |
| `ctest -R hud_portrait_position` | The portrait bounds the portrait taps are routed against |
| `ctest -R touch_portable` | That no touch owner branches on the platform it was built for, and that it inspected every owner rather than passing on an empty list (`tools/check_touch_portable.py`) |

These run in the ordinary suite on the ordinary host build, on every platform,
because the feature ships on every platform. None of them needs a device.

## Not established

- No touchscreen desktop run has been recorded. The classification and the
  layout are unit-verified and the code path is platform-neutral by
  construction, but "a Windows tablet was played on" is not a claim this
  repository can make yet. Measured device evidence for phones is separately
  gated in `docs/android-release.md`.
