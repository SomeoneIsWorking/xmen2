---
id: C018
kind: claim
status: holds
created: 2026-08-04
tags: 
---

## Claim

Functions with GLOBAL side effects cannot be verified by double-execution, and this is a property of the method rather than a defect in the translation.

## Evidence

6 failures at 392-case width are all such functions: igWin32Window::showCursor / hideCursor / setClipCursorRect / setClipCursorState call Win32 cursor APIs where ShowCursor returns a per-call counter, so the original's call changes what the recompiled call observes; igWin32ControllerManager::userConstruct and igTObjectList::setAll mutate shared state similarly. Running both sides is exactly what makes them differ.

## What would falsify it

Not all 6 are proven to be this: userConstruct and setAll were classified by inspection of what they touch, not by isolating the shared state. If either turns out to be a genuine translation bug it would be masked by this explanation -- they stay OUT of the verified set either way.
