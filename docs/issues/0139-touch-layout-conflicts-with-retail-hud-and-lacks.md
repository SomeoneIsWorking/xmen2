---
id: 139
title: Touch layout conflicts with retail HUD and lacks direct hero selection
status: resolved
symptom: Android touch overlay crowds gameplay with a second camera stick and controller glyph grid; retail HP/energy, potion, and portrait HUD stays in bottom-left and portraits cannot be tapped to select heroes
tags: android,touch,hud,input,ui
state_items: S018
created: 2026-08-31
updated: 2026-09-06
---

## Root cause

The first Android layout exposed controller-shaped controls directly from the
retail persistence vocabulary. Names such as `TargetLock` do not describe the
actual Xbox/PS2 binding (health pack), so a mechanically labeled overlay lied
about working actions. It also made camera movement a second visible stick and
left the retail CHud at its desktop bottom-left anchor. Touch contacts were
consumed before the ordinary SDL-to-Win32 mouse path, so portrait touches could
not reach the retail click handler that already performs direct hero selection.

## What was tried / dead ends

Cycling heroes by publishing new next/previous actions would duplicate the
retail portrait handler and would incorrectly imply that the working physical
D-pad bindings should be removed. The D-pad remains mapped exactly as retail;
only touch presentation uses the proven player-facing action names.

## Reopened 2026-09-02 -- the shipped layout is not what was asked for

The user's actual requirement, restated after seeing the build:

- The game's own HUD: HP/MP top-**left**, potion icons top-**left**, character
  faces top-**right**.
- Touch controls: movement stick bottom-**left**; abilities, light attack,
  heavy attack and use bottom-**right**.
- The touch UI is active **only while the player has character control** --
  not in menus, cutscenes or loading.
- The vanilla game is **mouse-playable**, so much of it is already touch
  compatible; the gaps are right-click (use / doors) and abilities. Leverage
  that path instead of emulating a pad for everything.
- Pad-style and touch/pointer gameplay are **both** offered and the player
  chooses.

Why the shipped version is wrong rather than merely unpolished:

- `src/presentation/touch_hud_layout.c` does not place anything. It MIRRORS the
  authored anchor (`x = width - x`, `z = height - z`) and mirrors the character
  panels back across X. Nothing is positioned deliberately, no element size is
  known, and the result is wherever the reflection lands.
- The portrait touch zones in `touch_controls.cpp` are an INDEPENDENT guess at
  where that reflection put the party cross (`right - radius*2`,
  `min(w,h)*0.07`). HUD position and its hit-zones are therefore two sources of
  truth that can only agree by luck.
- The action buttons are eighteen hand-tuned fractions spread over the whole
  right half and the bottom middle, not the requested bottom-right cluster.
- Visibility is gated on `x2_touch_runtime_overlay_visible()`, which is a
  setting, not a statement about whether the player controls a character.

## RE established 2026-09-02

- **The retail build really is mouse-driven, and the cursor feeds the same
  per-player input records the pad does.** `GetCursorPos` has exactly one
  caller in the 16,451-function corpus: `FUN_005f53d0`, which pairs it with
  `ScreenToClient` and returns client-space X/Y. Its single caller
  `FUN_005f5b00` is the per-frame input update: it converts that position to
  float and writes the pair into a table at stride 0x1C, at +0x1c/+0x20,
  +0x38/+0x3c, +0x54/+0x58, +0x70/+0x74 and +0x8c/+0x90 -- four player slots
  plus a base at +0x4. Input state objects hang off `[0x00a0a098]` and
  `[0x00a0a0a4]`; the window handle is `[0x00a0a004]`.
- The port already has the whole Win32 mouse path including the right button
  (`src/native/win32_mouse.{c,h}`: `X2_WM_RBUTTONDOWN`/`UP`, `X2_MK_RBUTTON`),
  so "use / open doors" needs no new plumbing -- only a caller.
  `touch_runtime.cpp` currently publishes pointer events for portrait zones
  only, and only as a left button.
- `FUN_005a43d0` (CHud root draw) is 917 instructions and `FUN_005a3320`
  (per-character panel) is 897. Element identity and authored sizes should be
  enumerated from a RUNNING build through the existing live instruments rather
  than read out of that disassembly.

Scratch RE helpers: `scratch/hud/dis.py`, `scratch/hud/xref.py`,
`scratch/hud/dump_fn.py` (corpus disassembly and address xref; both print the
number of functions and instructions searched, so "no callers" is a fact and
not an empty listing).

## The character panels were never relocated -- their COLOUR was

`x2_touch_hud_character_draw` reads its target vector from `RD32(C->esp + 8u)`.
Every override in this port reads its first guest stack argument at `esp + 4`
(`crt.c`, `gdi32.c`, `advapi32.c` all spell it `A(i) = RD32(esp + 4 + i*4)`),
so `esp + 8` is the SECOND argument.

The call site, decompiled, is

    FUN_005a3320(hero, &fStack_314, &uStack_2f8, index, flag)

with `&fStack_314` -- the position, filled by `(*(piVar7 + 0xdc))(iVar13,
&fStack_314, 0, local_208)` -- as the first stack argument, and `&uStack_2f8`
as the second. `uStack_2f8` is not a position:

    uStack_2f8 = 0x3f800000;   /* 1.0f */
    uStack_2f4 = 0x3f800000;
    uStack_2f0 = 0x3f800000;
    uStack_2ec = 0x3f800000;
    ...
    puVar8 = (*(piVar7 + 0x224))((int)fVar28 + 0xe);   /* per-player entry */
    uStack_2f8 = *puVar8; uStack_2f4 = puVar8[1];
    uStack_2f0 = puVar8[2]; uStack_2ec = puVar8[3];

Four components defaulting to (1,1,1,1) and otherwise fetched per player: an
igVec4f **player colour**. So with Android touch mode on, every character
panel was drawn after `x = width - x` had written 799.0f into its red channel
and its blue one, and the position it was supposed to move was never touched.
This is the bulk of what "awful" was.

Fix: the position is `esp + 4`. But the mirroring goes too -- see below.

## Layout authority (landed 2026-09-02)

`src/presentation/touch_layout.{c,h}` now computes every HUD element rectangle
and every touch zone from one viewport, so the two cannot disagree. Slots:
vitals, potions, portraits (retail elements this port moves) and stick,
light-attack, heavy-attack, use, powers (controls it draws).

`tests/test_touch_layout.c` asserts, over seven viewport shapes including a
cutout, a square and a portrait orientation: every rectangle inside the safe
area on all four edges, no two rectangles overlapping, the requested corners,
and that the stick and the action cluster stay on opposite sides. 616 checks.
It found three real defects on its first runs -- the action diamond overflowing
the bottom and right safe edges, the stick colliding with the cluster at 1:1,
and the top-left vitals landing underneath the top-right portraits at 1080x2400
because both spans were taken from the long edge rather than from the axis each
actually occupies.

## Superseded resolution (2026-08-31)

Implemented pending installed-APK visual verification. The touch action owner
now provides one left stick, labeled bottom-right combat/ability buttons, and
an invisible relative camera-swipe region backed by Lucent's capture origin.
Top-right portrait contacts publish ordered Win32 mouse movement and left-button
messages through the existing retail path. `FUN_005a43d0` remains the one CHud
draw path: a scoped native override mirrors its authored root to the top-right,
while the retained `FUN_005a3320` character-panel body is remapped to the
top-left. The original vectors are restored after each super-call, and desktop
behavior is unchanged because relocation requires the Android touch overlay.

## 2026-09-02: the root shift FRACTURES the HUD -- measured, then removed

Shifting the authored `igVec3f` at `CHud+8` does move part of the retained
tree: the selector wheel and the health/energy bars follow it. The four
character portraits do NOT -- they are placed from their own coordinates.
So the shift pulled the group apart on screen: portraits left in place, the
X wheel and the bars gone from between them. User-reported, then reproduced
and settled by two captures of the same scene (same save, same camera), one
with the shift and one without.

That falsifies the module's founding claim -- that `this+8` is "the origin
every element is laid out from". It is the origin of SOME of them.

The shift and the per-character hook are removed. `touch_hud_runtime.c` now
only publishes the gameplay-HUD heartbeat to the control gate, and the retail
HUD draws exactly as authored. `touch_layout.c` still owns the three HUD
slots -- where the elements are MEANT to go is decided and tested; what is
missing is per-element placement, which needs the scene-graph structure under
CHud element by element.

## Resolution 2026-09-05 -- scene-graph placement and direct portrait selection

The scene-graph structure under CHud is recovered and documented in
`docs/RE/hud.md`:

1. **Composition and draw entries.** `005a62c0` invokes `005a43d0` (party
   selector/cross), `005a3320` (vitals panel), `005a5170` (inventory/potions),
   and `005a1ab0` (portrait presenter) under their retail conditions.
2. **Scoped submission transformation.** `src/native/hud_draw_runtime.c`
   intercepts those four entries and activates a scoped affine transform
   (`x2_hud_fit` in `src/presentation/hud_layout.c`). During an active scope,
   2D sprite submissions (`0059a140`), 2D text submissions (`005f11b0`), and
   3D scene node matrices (`00570970`) are scaled and translated into their
   layout slot without fracturing element hierarchies or modifying assets.
3. **Mobile HUD layout.** Vitals (HP/MP) are placed top-left; potions (health
   and energy packs, scene Z extent [162, 202]) sit directly below vitals in
   top-left; and the four character portraits are arranged in a horizontal row
   in top-right. The console D-pad selector cross (`m_playercross`) is moved
   offscreen in mobile layout because hero selection routes directly through
   portrait tapping and its directional beams point away from the horizontal
   portrait row.
4. **Portrait position getter and hit selection.** `005a1650` is natively
   implemented in `src/native/hud_portrait_position.c` and verified against
   the guest body under `hud.verify`. `capture_portrait` positions each face
   into its slot and writes the transformed scene center to `PORTRAIT_CENTERS`
   (`0x00a0a0cc + slot*12`). Touch contacts in the published portrait regions
   are routed by `src/input/touch_controls.cpp` to Win32 mouse clicks, which
   hit the retail click handler (`005f9eb0`) and perform direct hero selection.
5. **Configuration and geometry verification.** `src/config/hud_settings.{c,h}`
   and `src/ui/hud_settings_document.{cpp,hpp}` provide layout selection (Auto,
   Retail, Mobile), scale percentages for vitals, potions, and portraits, and
   safe edge insets. `tools/hud_geometry.py` diffs root boxes against
   `src/presentation/hud_geometry.h` across 117 retail assets. 136 tests pass.

## 2026-09-06 -- the overlay and the mobile HUD follow the device in use

Both were still decided by the saved setting alone, so a desktop run drew the
thumbstick, the action cluster and the mobile HUD placement over a game being
played with a keyboard, and an Android player holding a controller got the
same overlay they were not touching. Platform is not the answer either: the
same build is played both ways on both platforms.

`src/input/touch_source.{c,h}` now classifies each host event, and it is the
only thing that decides. Touch becomes the active source at the first contact
and stops being it at the next key, real-mouse, or deliberate pad input. Two
events deliberately do NOT count: SDL's synthetic touch-as-mouse reports
(`SDL_TOUCH_MOUSEID`), which are the same finger seen twice, and a pad axis
inside half deflection, which is a resting or drifting stick. A device merely
being connected says nothing.

`x2_touch_runtime_active()` is the one answer both consumers ask --
`x2_touch_runtime_overlay_visible()` for the drawn controls (with a window and
gameplay control on top of it) and `hud_draw_runtime.c` for the mobile HUD
placement. They are one feature, so a HUD that relocates while no pad is drawn
would be the HUD making room for nothing.

`input.touch_controls` is now Off / Automatic / Always rather than a flag, with
Automatic the default on every platform; existing files keep their meaning (0
stays Off, 1 becomes Automatic). Always keeps the layout reachable on a desktop
with no touchscreen, which is the only place it can be iterated on.
`tests/test_touch_source.c` covers both answers and the two events that must
not flip it; `tests/test_settings.c` covers the tri-state and its labels.
