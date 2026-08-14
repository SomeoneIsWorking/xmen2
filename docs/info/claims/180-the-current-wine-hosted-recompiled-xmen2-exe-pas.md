---
id: C180
kind: claim
status: holds
created: 2026-08-14
tags: recomp,wine,graphics
---

## Claim

The current Wine-hosted recompiled XMen2.exe passes the same D3D presentation parameters as stock: 800x600, R5G6B5 colour, D16 depth, fullscreen, swap effect 1.

## Evidence

Fresh tools/build_x2run.sh build from scratch/recomp/XMen2.json, then X2_EXE=x2run.exe X2_SAMPLES=3 tools/run_shim.sh x2run 30 and X2_SAMPLES=3 tools/run_shim.sh stock 30. scratch/logs/x2run.log scanned 464 lines and scratch/logs/stock.log scanned 458; each contained exactly 1 D3D9DeviceEx::ResetSwapChain block and the requested fields matched exactly. The x2run log also reached both 800x600 display-mode lines, cg.dll/cgD3D8.dll load, and SetRenderState 26.

## What would falsify it

A fresh same-environment stock/x2run pair from the current translator produces different ResetSwapChain fields, or either run fails to reach exactly one ResetSwapChain block.
