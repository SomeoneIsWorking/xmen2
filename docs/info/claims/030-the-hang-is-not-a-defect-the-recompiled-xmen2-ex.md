---
id: C030
kind: claim
status: falsified
created: 2026-08-05
tags: 
falsified_on: 2026-08-05
---

## Claim

The 'hang' is not a defect: the recompiled XMen2.exe executes its own game code to display initialisation and puts up its OWN modal Win32 dialog ('Display failed! Unable to initialise graphic display. Resolution and FSAA have been reverted to default'). A modal message loop is why no further guest code runs.

## Evidence

Screenshot of the recompiled process under a Wine virtual desktop shows the runner's console output followed by the game's own titled dialog box. The watchdog reports 4102 recompiled calls then +0 per tick -- blocked in a host call, which is exactly what a modal MessageBox is. The ORIGINAL game produced the identical dialog in this environment before it was given a virtual desktop, so the failure is environmental (llvmpipe cannot satisfy the requested mode), not a translation defect. Feeding the 7 runtime-discovered fallback addresses back into Ghidra (AddFunctions.py) and re-recompiling dropped fallbacks from 7 to 1.

## What would falsify it

Setting multiSampleType=0 did NOT change it, so the FSAA theory is wrong and the exact environmental difference from the stock run that DID render is not identified. And this proves the game reaches display init -- not that it would render a frame if display init succeeded. No game loop, no movie, no title screen has been observed from recompiled code.

## FALSIFIED 2026-08-05

The 'environmental' conclusion was WRONG. Diffing the DXVK log of a stock run against the recompiled run at device creation shows the recompiled code passes GARBAGE presentation parameters: stock asks for 800x600 R5G6B5 with a D16 depth buffer, recompiled asks for Width=1, Height=43253760, A8R8G8B8, D24S8, and DXVK fails with 'Failed to create swapchain backbuffers'. The dialog is the game CORRECTLY reporting that D3D rejected its request. So this is a translation defect in the code that fills D3DPRESENT_PARAMETERS, not the headless environment -- and I reached the environmental conclusion from a surface similarity (the original showed the same dialog for a genuinely environmental reason earlier) instead of comparing the two runs. Three hypotheses were then tested against that wrong frame.

> Anything that cited this claim as proof must be re-checked. Grep the repo for it.
