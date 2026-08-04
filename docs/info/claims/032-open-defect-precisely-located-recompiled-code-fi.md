---
id: C032
kind: claim
status: holds
created: 2026-08-05
tags: 
reconfirmed: 2026-08-05
---

## Claim

OPEN DEFECT, precisely located: recompiled code fills D3DPRESENT_PARAMETERS with wrong values -- Width=1, Height=43253760 (0x02942A00), A8R8G8B8/D24S8 -- where the original produces 800x600 R5G6B5/D16.

## Evidence

Side-by-side DXVK D3D9DeviceEx::ResetSwapChain dumps from scratch/logs/stock.log and scratch/run/x2name/direct.log. Stock: Width 800, Height 600, Format R5G6B5, depth D16, Windowed false. Recompiled: Width 1, Height 43253760, Format A8R8G8B8, depth D24S8, Windowed false, followed by 'err: D3D9: Failed to create swapchain backbuffers'. Both runs use the same alchemy.ini (multiSampleType=0) and the same 800x600 virtual desktop.

## What would falsify it

Not yet traced to an instruction. Width=1 and a nonsense Height suggest a struct being filled through wrong offsets or widths -- candidates are the operand-width inference for untyped memory operands, a 16-bit store landing as 32-bit, or a bad stack offset in the caller. Find the function that writes the structure (it will be an igWin32Window/igGfx path reached just before ResetSwapChain) and difftest it.

## Re-confirmed 2026-08-05

CONTROLLED comparison, not just two logs: the ORIGINAL XMen2.exe run from the SAME directory with the SAME environment, virtual desktop and alchemy.ini produces Width=800 Height=600 R5G6B5/D16, while the recompiled exe produces Width=1 Height=43253760 A8R8G8B8/D24S8 and fails to create backbuffers. Only the executable differs. This is a translation defect with certainty now.
