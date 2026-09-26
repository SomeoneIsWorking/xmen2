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
virtual DirectInput pad — the whole of the table below. The exceptions are
the party portraits and the pause and team menu icons, which belong to the
retail mouse handler, and the port menu button, which opens the port's own
RmlUi settings.

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

**Every screen that is not gameplay draws the menu pad** -- the front end,
the pause and team menus, the World Map, dialogue. The retail menus are
navigated with a controller exactly as on the Xbox, so the port draws that
controller: a d-pad bottom left where the stick sits in gameplay, the face
buttons bottom right in the Xbox arrangement (A below, B right, X left, Y
above), and a shoulder above each cluster for tabbed screens
(`x2_layout_build_menu`, `x2::input::MenuControls`). They publish through the
same virtual pad as the gameplay controls, so the footers name that pad's
buttons -- `B Back` beside a B -- in the shared Xbox glyphs the buttons are
drawn from. A finger that begins on a pad button holds it until it lifts; one
that begins anywhere else is the retail pointer for its whole life, so a drag
across the pad never presses it. A held button is let go when gameplay or a
cinematic takes the screen, even if the finger never moves.

The port once made the footer's words themselves tappable instead, pairing
each prompt with the draw that placed it by glyph count. The World Map's level
names draw as many glyphs as its footer, so Back and Go were published over
`Sanctuary` and `Grand Hall` and the screen could not be left (issue #190).
Touch-native menus that replace the retail ones are future work; until then
the controller is the one input every retail menu is complete for.

**A cinematic that holds the controls offers a Skip button** in the top-right
corner, the phone's Escape. The cutscene player offers the skip on every input
poll while an authored sequence holds the player's controls, and it takes a
touch request on the same poll as the Escape edge, running the same owned
completion (`src/input/cutscene_skip.h`). The button draws from its own
document (`src/ui/skip_document.cpp`), because the gameplay overlay is hidden
then. A tap after the offer has ended is refused rather than held, so it
cannot skip the next cinematic. `/controls` lists it as `skip button` while
it is drawn.

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
| Left virtual stick | `Forward`, `Backward`, `MoveLeft`, `MoveRight`, from the contact's own capture origin; a thumb landing anywhere in the lower-left reach (`x2_layout_stick_reach`) takes it |
| Bottom-right action diamond | Attack (A) below, Smash (B) outside, Use (X) above, Jump (Y) inside |
| Arc inboard of the diamond | One button per RT power the hero actually has, drawn with the game's own icon; pressing it holds RT with that slot's face button |
| Retail party portraits, top-right | Pointer press/release through the existing Win32 mouse-message path; the retail click handler selects the tapped hero |
| Retail potions, under the vitals | Each in its own ring: health uses a health potion (`HealthPack`), energy an energy potion (`EnergyPack`) |
| Retail pause and team menu icons, top centre | A click on the icon through the same pointer path; the retail handler opens the pause or team menu |
| Physical D-pad | Next hero, previous hero, decrease aggression, increase aggression; these retail bindings remain valid |
| Gear beside the retail menu icons, top centre | `PortMenu`: opens the port's RmlUi settings, the touch player's F2 |
| Open playfield swipe | Relative camera movement from the contact's Lucent capture origin; no second visible stick |
| Retail health/energy HUD, top-left | The retained CHud draw path, relocated only while touch mode is active |

The stick steers from where the thumb landed, not from where the ring is
drawn (`src/input/thumb_stick.{h,cpp}`). A thumb does not arrive on the middle
of a circle it cannot see, and measured from the ring that landing offset is
itself an input: the character used to walk off the moment the screen was
touched, with a short push one way against a long reach the other
([issue 181](issues/0181-the-movement-stick-steers-from-the-ring-not-the-thumb.md)).
One ring radius of travel from the contact is full deflection, the result is
clamped to the unit circle rather than per axis so the diagonals cannot outrun
every other direction, and travel inside 8% of full is the thumb resting.
The knob is drawn at that live deflection, so the one control with a value
rather than a state shows its value.

The stick floats, as mobile games' sticks do. A thumb landing anywhere in the
lower-left reach takes it, not only one landing on the ring. The reach runs
from the potions down to the bottom edge, and stops at the centreline and at
the nearest action or power. While the thumb is down, the ring is drawn
centred on it. A thumb pushed past the rim drags the centre along behind it,
so reversing direction turns at once instead of first travelling back across
the landing point. Travel is still one ring radius.

The movement stick is smaller than the original overlay to leave more of the
playfield visible. Jump is on the opposite hand from movement, so a player can
move and jump with two thumbs. Controls remain anchored to safe edges on wide screens; one shared fit
factor, the smaller of what the width and the height below the portraits
allow, keeps the groups separate on narrow, short and portrait screens. Every
round button, action or power, is one size. The port menu button joins the
row of menu icons the game's mouse overlay draws at the top centre: once the
game reports where it drew them, the button takes the next place along that
row at their size (`TouchControls::port_menu_rect`), and until then it waits
just right of the centreline. The vitals and party portraits start at that
row's top too (`x2_hud_layout_build`'s `row_top`), so the top band is one
line even on a phone whose reported safe area starts lower than where the
game draws its own icons.

The layout must leave an inset for cutouts/navigation bars, support at least the
left stick plus two face/shoulder contacts simultaneously, expose a
reconfigure/hide-controls setting, and make touch feedback visible without
changing the input action delivered to the guest. The mapping is derived from
[`xbox_defaults.c`](../src/native/xbox_defaults.c), not invented per screen. Every
button is a round icon filling a dark circle with a light ring. A power shows
its own icon from the hero's atlas. Attack, Smash, Use, Jump and the port menu
show the port's own art, `assets/ui/touch_{punch,smash,use,jump,menu}.svg`:
neon line icons in the style of the retail pause and team icons (a saturated
outline, a soft glow and a pale core; orange for the attacks and the menu,
blue for Use and Jump). They are drawn without filters, so SDL_image's SVG
rasterizer renders them identically on every platform, at 384 pixels, which
RmlUi scales down to the button: crisp at a phone's size. The game's own
talent icons are 64-pixel cells and looked soft scaled up that far.
Portraits, potions and the retail menu icons are drawn by the game, so their
controls are the same ring with no fill; the port menu, in their row, is drawn
the same way. Active controls get a bright border and filled background. The camera
gesture stays invisible, captured zones highlight, and the persistent Input setting can hide the controls. Held contacts
persist until finger-up/cancel rather than expiring on a test-channel timeout.

### Power buttons

The retail HUD shows a hero's powers only while RT is held: CHudInputMap's
update (`FUN_005a6c60`) draws four circles, slot *i* showing the power named at
`stats + 0xc4 + i*0x15` of `FUN_0041d5a0(actor)`, resolved to a move through the
actor's power styles, and empty when it does not resolve. The cast side
(`FUN_004fc970`, table `0x006dc37c`) pairs slots 0..3 with action bits 4, 5, 8,
6 -- LowAttack, HighAttack, Guard, Jump, which the Xbox preset binds to A, B,
X, Y. The touch layout drops the separate held Powers modifier and offers each
slot as its own button instead.

`src/native/power_slots_runtime.c` overrides that update, runs the game's body,
and then -- only while touch is the input, and only when the actor or its four
slot names changed -- asks the same functions the ring asks. A move's `icon`
is the byte at `+0x13c`; its style (`move->vfunc 0xe0`) keeps the `iconfile`
name as a string-pool handle at `+0x68`. The runtime hands the four atlas
cells to `x2_touch_runtime_power_slots`, which builds a zone only for a slot
with a power, so a locked or unassigned power has no button. A press routes to
`TouchAction::PowerN`, which the pad publisher turns into RT plus that face
button; a button shared by several held controls stays down until the last
one lets go.

`src/ui/igb_textures.{hpp,cpp}` loads the atlas from the player's install
(`Textures/ui/<hero>_icons1.IGB`, a 4x4 grid of 32x32 icons in 128x128) through
`shared/alchemy`'s reader. Its rows are stored bottom first: the ring draws
Magneto's innate power (`icon="4"`) from the third stored row, upside down
unless reversed. The corners are opaque black under the game's ring frame, so
each cell is cut to its inscribed circle.

Observed 2026-09-24 in the tutorial with Magneto (`input.touch_controls=2`,
Xvfb): the runtime resolved `textures/ui/magneto_icons1.png` and icons
`4 -1 -1 -1`, the overlay drew one power button with the game's icon, and
holding it raised the retail RT ring -- the chord reached the game. That the
chord casts is C224's evidence (RT+A with the same pad codes).

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
| `ctest -R thumb_stick` | The stick's own policy: an off-centre landing steering nothing, equal travel in every direction, the circular clamp, the dead zone and its rescale, release, and a ring with no size |
| `ctest -R touch_source` | The device classification, including the `SDL_TOUCH_MOUSEID` synthetic pointer and the resting-stick threshold |
| `ctest -R touch_controls` | Action vocabulary, independent four-ability modifier chords, zone routing, portrait pointer arbitration, cancellation on layout change |
| `ctest -R touch_runtime` | The whole chain on the real synthetic pad: a press at the drawn control's own coordinates reaching the gamepad the game reads, the player-one claim, a contact outside every zone pressing nothing, stick rest/drag/release, power buttons drawn only for published slots and chording RT with their own face button, a button shared by two held controls staying down until both lift, a vanished power releasing, cancellation on focus loss, the census the report is made of, and — with no control drawn — a contact becoming a retail pointer press at its own position, pressing no pad button, refusing a second finger, and releasing when the first lifts |
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

### The menu pad, 2026-09-26

`tools/live_case.py menu-pad` reaches the main menu, reads `/controls` for the
buttons the run is drawing and the surface they are in, taps d-pad Down five
times and A, and requires the game's own first open of the Options package. B
must then return to the main menu, judged on the menu column's static art
rather than the whole frame, whose animated backdrop moves as much as a
screen change: 11.5 from the main menu against 49.3 from Options. 12 of 12.

### Native presentation observation, 2026-09-25

The [actual RmlUi/game capture](screenshots/touch-controls.png) is the shipping
renderer's final frame at 2728×1264, a phone's shape, shown at half size
(the control channel's `/screenshot`),
from a windowed run in a private Xvfb display with an isolated profile forcing
`input.touch_controls=2` and SDL dummy audio. The opening conversation's
last line was continued by a tap on it. In control, the overlay shows the move
stick, the four actions inside their own circles, the port menu button, and the power button with the game's own icon for the hero's power. The vitals,
potions and party portraits stay clear. This is native UI presentation
evidence, not Android touchscreen or performance qualification. A headless
`--no-window` run draws no overlay, so its captures cannot stand in for this.

## Driving it without a touchscreen

A host with no touchscreen cannot press its own screen, so the control channel
carries a contact: `/touch?x=&y=` (`tools/x2ctl.py touch 0.2,0.73`), with a
whole tap by default and `&phase=down|motion|up|cancel` for one half of one.
It goes through `x2_touch_inject`, which takes the same note-source and
routing calls the host event pump takes — a second copy of that sequence
could only agree with the shipping one by luck.

`/controls` lists what the overlay draws right now, one control per line with
its rectangle and action name (`menu-a`, `jump`, ...), and the viewport those
rectangles are in, so a run driving itself taps what the game is drawing
rather than a coordinate chosen before the run started.

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
