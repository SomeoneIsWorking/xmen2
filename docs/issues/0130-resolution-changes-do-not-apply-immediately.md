---
id: 130
title: Resolution changes do not apply immediately
status: resolved
symptom: Changing the configured output resolution leaves the current window/render output at the old size until a later restart or transition
state_items: S008
tags: resolution,settings,presentation,user-report
created: 2026-08-27
updated: 2026-08-28
---

## Root cause

Port Settings updates the persistent `X2Settings` and SDL window geometry, but
the active game render size remains the `D3DPRESENT_PARAMETERS` copied by
`IDirect3D8::CreateDevice`. The host left `IDirect3DDevice8::Reset` unimplemented
and created its backbuffer/depth surfaces for the lifetime of that device, so
neither the game-visible display mode nor the SDL_GPU scene targets change.

Retail does not contain a reusable live transition hidden behind its Options
screen. XMen2.exe `0x006180e0` only updates the resolution control/dirty state;
the apply path `0x0061f380` saves display settings through `0x00619440` and
then presents its `NEW_RESZ_RESTART` dialog. Its adjacent vtable call reaches
the controller manager, not a camera or menu-layout refresh.
Immediate application is therefore a native presentation enhancement whose
transaction has to update every active presentation owner together.

The first live transaction updated those host owners but missed the title's
retained display singleton and output-width/output-height globals. That left
the camera and menu transforms initialized for 800x600 while the new target
was 1280x720, producing the observed 4:3 image stretched across 16:9. The game
already initializes those fields for native widescreen during a cold launch;
the live path now preserves that same relationship.

## What was tried / dead ends

Changing only the SDL window is the current defect, not a solution. Removing
the RmlUi restart text without rebuilding game-space presentation would merely
hide the stale backbuffer.

## Resolution

Port Settings now applies resolution as one transaction owned by
`presentation/live_resolution`: it prepares a replacement SDL_GPU colour/depth
pair, commits the active D3D8 presentation parameters, backbuffer and depth
surface descriptions, full-backbuffer viewport, and the title display
dimensions/aspect/pixel scales, then resizes the SDL window and persists the
setting. Allocation, title-state, window, or persistence failure restores the
previous game-space targets, title state, window state, and settings together.
The RmlUi status reports the active game-rendering size instead of promising a
later restart.

The `d3d8_live_resolution`, `gpu_present_resize`, and `live_resolution` tests
exercise the shipping seams, including colour-allocation refusal,
depth-allocation refusal, window refusal, persistence refusal, and rollback
failure reporting. The transaction is refused during an open D3D8 frame rather
than replacing resources still referenced by a command buffer.

The visible `live-resolution` case passed 8/8: it changed an existing 800x600
window and render target to 1280x720, verified persistence, restarted the same
profile, and compared the live menu geometry with the cold native-widescreen
layout (0.875 overlap for the static orange menu geometry, above the 0.75
gate). This specifically fails if only the surface becomes 16:9 while the old
4:3 title transforms are stretched into it.

### Note (2026-08-27)
Clarification: SDL window resizing already happens immediately. The defect is that the game/D3D backbuffer remains at the old resolution and RmlUi explicitly says game rendering changes only after restart. Completion requires live device/game-space reconfiguration, not text removal or window-only resizing.
