---
id: C027
kind: claim
status: holds
created: 2026-08-05
tags: 
---

## Claim

The recompiled XMen2.exe drives real ENGINE initialisation, not just CRT startup: it loads the original engine DLLs, creates a D3D9 device and swap chain, sets an 800x600 display mode, loads the Cg shader runtime (cg.dll/cgD3D8.dll) and issues render-state calls.

## Evidence

tools/run_shim.sh x2run with X2_EXE=x2run.exe. Log shows D3D9DeviceEx::ResetSwapChain, 'Setting display mode: 800x600@0', cg.dll and cgD3D8.dll loaded, and 'SetRenderState: Unhandled render state 26' -- the same DXVK messages the ORIGINAL game produces. Reached after four fixes: Ghidra function coverage raised 77.5%->87.3% (FillFunctions.py, +2388 functions, conservative: only at referenced addresses), RET honouring redirected return addresses, IAT patching, and host-call routing for indirect targets outside the image.

## What would falsify it

It renders NOTHING -- three frame samples were all uniform -- and the process exits rather than reaching a game loop. Why is undiagnosed. Also: hybrid fallback is ENABLED, so 5 addresses ran as ORIGINAL machine code rather than recompiled; with more execution that count will grow, and a build that quietly runs the original is not a recompilation. X2_NO_FALLBACK=1 turns it off and makes those aborts.
