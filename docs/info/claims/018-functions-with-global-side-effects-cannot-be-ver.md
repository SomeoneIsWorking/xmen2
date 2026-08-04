---
id: C018
kind: claim
status: holds
created: 2026-08-04
tags: 
reconfirmed: 2026-08-04
---

## Claim

Functions with GLOBAL side effects cannot be verified by double-execution, and this is a property of the method rather than a defect in the translation.

## Evidence

6 failures at 392-case width are all such functions: igWin32Window::showCursor / hideCursor / setClipCursorRect / setClipCursorState call Win32 cursor APIs where ShowCursor returns a per-call counter, so the original's call changes what the recompiled call observes; igWin32ControllerManager::userConstruct and igTObjectList::setAll mutate shared state similarly. Running both sides is exactly what makes them differ.

## What would falsify it

Not all 6 are proven to be this: userConstruct and setAll were classified by inspection of what they touch, not by isolating the shared state. If either turns out to be a genuine translation bug it would be masked by this explanation -- they stay OUT of the verified set either way.

## Re-confirmed 2026-08-04

Widening the object shapes raised failures from 1 to 6, and 4 of the 5 new ones are the same global-state category, now with clearer evidence: igWin32Window::getNativeDeviceHandle returns 0x5b01004a vs 0x0e010040 (distinct HDC/HWND handles -- GetDC hands out a new one per call), getKeyState reads live keyboard state, repos calls a window-positioning API, setCursorPositionNormalized moves the real cursor. None are comparable by double-execution.
