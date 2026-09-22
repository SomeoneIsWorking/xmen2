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

## Where a contact goes

A finger reaches the guest by one of two routes, and which one is decided by
whether there is a drawn control under it.

**With the overlay up**, the contact goes to its zone and the zone to the
virtual DirectInput pad — the whole of the table below. The one exception is
the party portraits, which belong to the retail mouse handler.

**With no overlay drawn — the legal splash, the intro movies, the main menu,
the load and save screens, every cutscene — the contact IS the retail GUI's
pointer**, at its own position. Those screens are the retail GUI and the
retail GUI takes a mouse (issue #132 put `igWin32Window::getEvents` back in
the loop), so a tap is a click on what the player can see.

Contacts there used to be counted and discarded, which is the whole of what a
phone player met: an intro no tap could skip and a menu no tap could press
(issue #179). They had no second route either, deliberately — `x2native.c`
sets `SDL_HINT_TOUCH_MOUSE_EVENTS=0` so that an action-pad tap cannot also
reach the retail world-click handler.

**Where the screen offers an action by naming a key** — the footer's `Esc
Back`, `[Space] Advanced Options` — the key is taken off what is drawn and
what remains becomes a control. The key's glyphs are collapsed where the
emitter writes them, the words slide into the space they left, and the
rectangle they landed in is published; a contact inside it presses the
DirectInput code the prompt named, retained when the cap was composed and the
binding had just been named. The control draws no art: the words retail
already drew ARE the button, and the port's own outline would cover them.

Which draw places a prompt is decided by that draw's own glyph count, not by
the order prompts arrive in. A frame lays every prompt out and only then
draws them, one element per draw with its own world matrix; a glyph occupies
six vertices and a draw declares two fewer primitives than vertices, so the
draw of a 15-glyph `Back` declares 88 and the draw of a 32-glyph `Advanced
Options` declares 190. Pairing them by arrival instead drew `Back` on top of
`Advanced Options` (issue #180).

Both numbers are the engine's: the text sink's write cursor advances six per
emitted glyph, and consecutive draws in a text pass begin where the last one
ended plus two. It is still a match on length rather than identity -- two
prompts of equal length on one screen could take each other's transform --
because the sink's cursor and the draw's start-vertex argument turn out to
count in different spaces, and matching by vertex range put a dialog's two
footer prompts on two lines the screen draws as one. Mis-attribution is
geometric, so `tools/live_case.py prompt-touch` requires the footer it reads
to be side by side on one line.

Retail draws one cursor and has one button, so one contact owns it at a time:
`x2::input::PointerOwner` is that rule, shared by the portrait tap and the
menu tap rather than copied into each. A button pressed by a finger is always
released by something — a lifted finger, a lost window, or gameplay starting
underneath it.

## The layout and the action vocabulary

The title owns the safe-area-aware layout and action vocabulary in
`src/input/touch_controls.cpp`, while `src/input/touch_runtime.cpp` converts SDL
contacts to the existing virtual DirectInput pad through `lucent::touch::Router`.
The Android framework owns raw Activity contact capture and lifecycle cancellation;
Lucent's platform-neutral router owns action-zone capture and multi-touch routing,
not the title's action vocabulary. A contact stays with its zone after leaving
the zone until it ends or is canceled. The runtime derives safe-area insets from
SDL and publishes releases on cancellation, rotation, or lifecycle loss.

The landscape layout uses these zones and the existing Xbox-derived action
rows. Internal retail storage names are not player-facing labels: the touch
document uses the action meanings proven by `binding_rows.c` and
`xbox_defaults.c`.

| Zone | Action mapping |
|---|---|
| Left virtual stick | `Forward`, `Backward`, `MoveLeft`, `MoveRight` |
| Bottom-right action diamond | Attack below, Smash outside, Use above, Jump inside; while Powers is held these are the four retained ability actions |
| Above the left stick | Hold Powers with the left thumb and choose an ability with the right |
| Retail party portraits, top-right | Pointer press/release through the existing Win32 mouse-message path; the retail click handler selects the tapped hero |
| Physical D-pad | Next hero, previous hero, decrease aggression, increase aggression; these retail bindings remain valid |
| Top button beside vitals | `Pause` |
| Open playfield swipe | Relative camera movement from the contact's Lucent capture origin; no second visible stick |
| Retail health/energy HUD, top-left | The retained CHud draw path, relocated only while touch mode is active |

The movement stick is smaller than the original overlay to leave more of the
playfield visible. Jump is on the opposite hand from movement, so a player can
move and jump with two thumbs. The Powers modifier is on the left for the same
reason: all four ability choices remain accessible to the right thumb while it
is held. Controls remain anchored to safe edges on wide screens; one shared fit
factor keeps the groups separate on narrow and portrait screens. The compact
pause button stays between the vitals reservation and the centerline, leaving
the retail center status/notification icons visible.

The layout must leave an inset for cutouts/navigation bars, support at least the
left stick plus two face/shoulder contacts simultaneously, expose a
reconfigure/hide-controls setting, and make touch feedback visible without
changing the input action delivered to the guest. The mapping is derived from
[`xbox_defaults.c`](../src/native/xbox_defaults.c), not invented per screen. The
shipped feedback document mirrors authored touch controls with short action labels
and bold outlined SVG silhouettes from `shared/port-assets/sets/touch-controls`.
`tools/touch_icons.py` resolves that manifest for build-time staging; the port
does not carry another SVG copy. Ability icons and labels change while Powers is
held; redundant tiny corner badges are absent. Button accent colors supplement
the distinct silhouettes, and active controls get a bright border and filled
background. Gesture and portrait hit regions remain invisible, captured zones
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
| `ctest -R touch_controls` | Action vocabulary, independent four-ability modifier chords, zone routing, portrait pointer arbitration, cancellation on layout change |
| `ctest -R touch_runtime` | The whole chain on the real synthetic pad: a press at the drawn control's own coordinates reaching the gamepad the game reads, the player-one claim, a contact outside every zone pressing nothing, stick rest/drag/release, the Powers chord staying whole under a second thumb, cancellation on focus loss, the census the report is made of, and — with no control drawn — a contact becoming a retail pointer press at its own position, pressing no pad button, refusing a second finger, and releasing when the first lifts |
| `ctest -R touch_layout` | Safe-area-aware placement across nine phone/tablet/desktop shapes, opposite-thumb reach, nonoverlapping HUD/control bounds, and at least 48 output-pixel action targets in those cases |
| `ctest -R hud_layout` | The pure HUD edge-relocation policy |
| `ctest -R hud_portrait_position` | The portrait bounds the portrait taps are routed against |
| `ctest -R touch_portable` | That no touch owner branches on the platform it was built for, and that it inspected every owner rather than passing on an empty list (`tools/check_touch_portable.py`) |

These run in the ordinary suite on the ordinary host build, on every platform,
because the feature ships on every platform. None of them needs a device.

### The screens before gameplay, 2026-09-22

`tools/live_case.py menu-touch` drives real contacts into a real boot through
the control channel's `/touch` route and checks both halves of issue #179
against a control that must come out the other way. A tap ended the first
intro movie 6.5s in, having been made at 6.0s, where the untouched movie runs
10.0s; a tap on empty sky opened nothing and a tap on the OPTIONS row produced
the game's own first open of `menus/options.pkgb`. The census reported 6
contacts reaching the retail pointer and none dropped.

Neither measure is the obvious one, and the obvious ones are both wrong here.
The frame counts in a movie's end summary cannot say whether it was skipped —
the decoder runs ahead and the summary is printed at unload, so a skipped
movie reports the same 312 decoded frames. A pixel delta cannot say whether a
menu responded — the menu animates, and its idle frame-to-frame difference
measured 11–19 against the 27 a working tap produced.

### The footer prompts as controls, 2026-09-22

`tools/live_case.py prompt-touch` opens Options, reads `/prompts` for what the
run says is pressable and in which surface, taps the Back control it names,
and requires the screen afterwards to be a different one — stated by which
prompts it draws, Options' own pair being Escape and Space. The census must
also count the press and report no refusal from the keyboard injector. 9 of
9, three runs in a row.

A pixel delta cannot make that call, for the same reason it could not in
issue #179: the menu animates, and a run in which the tap HAD worked measured
40.96 from the Options screen against 42.54 from the menu it returned to.

The main-menu check is the negative one: that screen draws no action prompt,
so nothing may be pressable on it. It is what would catch a control outliving
the screen that drew it, the difficulty dialog's own `Esc Back` having been
published seconds earlier and staying pressable for two.

`/prompts` reports the viewport its rectangles are in, because a caller that
divides by a window size learned somewhere else taps a fraction of the wrong
surface: that is how a tap aimed at `Back` in a 1280x720 window went off the
bottom of it.

### Native presentation observation, 2026-09-08

The [actual RmlUi/game capture](screenshots/touch-controls.png) shows the shared
SVGs in the shipping Vulkan renderer at 1280×720, with an isolated profile forcing
`input.touch_controls=2`, Xvfb, and SDL dummy audio. The tutorial cutscene ended
through the normal Escape cancellation route (one request, one completion,
controls released); the overlay then appeared during character control. Pause
leaves the retail center notifications clear, and the vitals, potions, and party
portraits remain visible. The live JIT report counted 383,048,017 block entries,
94,546 translations, and zero refusals in 94,546 attempts. This is native UI
presentation evidence, not Android touchscreen or performance qualification.

## Driving it without a touchscreen

A host with no touchscreen cannot press its own screen, so the control channel
carries a contact: `/touch?x=&y=` (`tools/x2ctl.py touch 0.2,0.73`), with a
whole tap by default and `&phase=down|motion|up|cancel` for one half of one.
It goes through `x2_touch_inject`, which takes the same note-source and
routing calls the host event pump takes — a second copy of that sequence
could only agree with the shipping one by luck.

`/prompts` (`tools/x2ctl.py` reads it too) lists the action prompts that are
pressable right now, their rectangles, and the viewport those rectangles are
in, so a run driving itself taps what the game is drawing rather than a
coordinate chosen before the run started. An empty answer says which empty it
is: no prompt is on screen, or touch play is not on and none ever will be.

## Not established

- No touchscreen desktop run has been recorded. The classification and the
  layout are unit-verified and the code path is platform-neutral by
  construction, but "a Windows tablet was played on" is not a claim this
  repository can make yet. Measured device evidence for phones is separately
  gated in `docs/android-release.md`.

## The browser

Touch play ships on the web target with no title code of its own: the same
owners decide the vocabulary, the layout and the routing there. What the browser
adds is that the *page* must give the canvas its gestures first. A browser owns
scroll, pinch, long-press and overscroll on any element the page has not
claimed, and the pad lives on the canvas, so a thumb drag on the virtual stick
scrolls the document instead — measured at 551 px before this was fixed (issue
#170). `shared/web-port`'s `claimCanvasGestures`, called from `web/app.mjs`,
takes them, and `tools/web_touch_probe.py` drives a real emulated touch device
to check that it still does.

Two things remain untrue in a browser and are recorded in issue #170: no run has
yet shown a touch on a drawn control moving the game, and `SDL_GetWindowSafeArea`
still reports the whole canvas there, so a control against the edge can sit under
a notch or the home indicator.
