---
id: 130
title: Resolution changes do not apply immediately
status: active
symptom: Changing the configured output resolution leaves the current window/render output at the old size until a later restart or transition
state_items: S008
tags: resolution,settings,presentation,user-report
created: 2026-08-27
updated: 2026-08-27
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
rebuilds menu/UI data through the menu-manager vtable call at `0x005535a0`.
Immediate application is therefore a native presentation enhancement whose
transaction has to update every active presentation owner together.

## What was tried / dead ends

Changing only the SDL window is the current defect, not a solution. Removing
the RmlUi restart text without rebuilding game-space presentation would merely
hide the stale backbuffer.

## Resolution

Port Settings now applies resolution as one transaction owned by
`presentation/live_resolution`: it prepares a replacement SDL_GPU colour/depth
pair, commits the active D3D8 presentation parameters, backbuffer and depth
surface descriptions, and full-backbuffer viewport, then resizes the SDL
window and persists the setting. Allocation, window, or persistence failure
restores the previous game-space targets, window state, and settings together.
The RmlUi status reports the active game-rendering size instead of promising a
later restart.

The `d3d8_live_resolution`, `gpu_present_resize`, and `live_resolution` tests
exercise the shipping seams, including colour-allocation refusal,
depth-allocation refusal, window refusal, persistence refusal, and rollback
failure reporting. The transaction is refused during an open D3D8 frame rather
than replacing resources still referenced by a command buffer.

### Note (2026-08-27)
Clarification: SDL window resizing already happens immediately. The defect is that the game/D3D backbuffer remains at the old resolution and RmlUi explicitly says game rendering changes only after restart. Completion requires live device/game-space reconfiguration, not text removal or window-only resizing.
