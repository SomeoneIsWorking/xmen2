---
id: 78
title: The D3D8 device held 16 lights; the engine uses 51, so 63% of every SetLight was refused in silence
status: resolved
symptom: characters render black in gameplay while the level, HUD and menus are correct
tags: rendering,lighting,d3d8,engine,recomp
created: 2026-08-15
updated: 2026-08-24
---

## What was measured

A logging `d3d8.dll` (`tools/proxy_d3d8`, staged by `tools/build_stocklog.py`) was put in front of the STOCK game under Wine and the game was driven into gameplay by hand. 924,858 calls logged.

| | control (original engine) | port |
|---|---|---|
| highest light index used | **51** | capacity was **16** |
| SetLight at index >= 16 | 259,960 of 411,873 (63.1%) | all refused, `D3DERR_INVALIDCALL` |
| LightEnable at index >= 16 | 129,980 of 225,800 (57.6%) | all refused |
| distinct indices in gameplay | 44 (0-14, 16-43, 47) | at most 16 |
| black diffuse in gameplay | **0 of 139,536** | -- |

## Why it was invisible

D3D8 does not oblige a caller to check SetLight's HRESULT, and this engine does not. The refusal returned an error code to nobody and printed nothing, so a light this device DROPPED looked exactly like a light the engine never set. Three sessions of instrumentation on our side of the boundary (C195, C196, C198, C199) all measured the light table we had, correctly, and could not see the 28 indices that never arrived.

The model was also simply wrong: in D3D8 the index names a slot in a list the runtime GROWS, and what is capped is how many may be ENABLED simultaneously (`D3DCAPS8::MaxActiveLights`), which is a separate limit enforced at the draw and already implemented.

## The fix

- `D3D8_MAX_LIGHTS` 16 -> 256, following `D3D8_MAX_RENDER_STATES` and for the same stated reason: a table generous enough for what the API is used with, and an index past it refused BY NAME.
- `light_index_refused()` names the first eight refusals and counts all of them; `d3d8_setlight_report` prints the count AT ZERO with its denominator and the highest index asked for.

## Resolution

The capacity defect is fixed independently of whether it explained every
historical black-character frame. `D3D8_MAX_LIGHTS` is 256; `SetLight` and
`LightEnable` accept stock-observed slot 51; state blocks and draw translation
preserve the slot; and `d3d8_host` drives slot 51 through both production vtable
setters and the fixed-function lighting pixel path. `MaxActiveLights` remains a
separate limit of eight. The focused `d3d8_host` test passed on 2026-08-24.

This resolves the API-capacity defect, not the old screenshot's complete causal
chain. The historical Cyclops dialogue divergence is tracked in issue #77 and
remains open pending a current capture with the same ordered draw signature.
