---
id: C200
kind: claim
status: holds
created: 2026-08-15
tags: rendering,lighting,d3d8,engine
---

## Claim

The original engine uses light indices up to 51; the port's D3D8 device held 16 and REFUSED 63% of every SetLight the engine made

## Evidence

Measured at the D3D8 boundary of the STOCK game under Wine with tools/proxy_d3d8 (a logging d3d8.dll that forwards every call), driven by hand into gameplay: 924,858 logged calls, highest light index 51, 259,960 of 411,873 SetLight calls (63.1%) and 129,980 of 225,800 LightEnable calls (57.6%) at index >= 16. Over the last 300,000 lines (the gameplay segment) the engine uses 44 distinct indices -- 0-14, 16-43, 47 -- and 0 of 139,536 SetLight calls carry a black diffuse. The port's D3D8_MAX_LIGHTS was 16 and dev_SetLight/dev_LightEnable returned D3DERR_INVALIDCALL above it, silently, so those lights never reached a draw and the engine (which does not check the HRESULT, and is not obliged to) could not tell. D3D8 places no limit on the index: SetLight names a slot in a list the runtime grows, and what is capped is how many may be ENABLED at once. At the MENU the port and control light streams are IDENTICAL (13 indices, same types, same diffuse/ambient/range/attenuation, same enable pattern), which is what makes the gameplay difference attributable.

## What would falsify it

a driven port run whose own X2_LIGHTLOG, compared with tools/lightlog_diff.py against the control's, shows the characters still black with every index now accepted -- which would mean the index cap was real but not the cause
