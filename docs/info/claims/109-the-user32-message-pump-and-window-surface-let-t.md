---
id: C109
kind: claim
status: holds
created: 2026-08-06
tags: pc,recomp,native,host,user32
---

## Claim

The USER32 message pump and window surface let the run reach shutdown, and expose a two-allocator split

## Evidence

Implemented 21 more USER32 entry points: the message pump (PeekMessageA, GetMessageA, TranslateMessage, DispatchMessageA, DefWindowProcA) translating SDL events, window state (Get/SetWindowLongA, SetClassLongA, SetWindowPos, SetWindowTextA, IsWindow, GetParent, EnableWindow, GetMenu, GetDC), GetSystemMetrics from the real display mode, and the geometry helpers ScreenToClient and PtInRect. PeekMessageA returning FALSE is the correct answer for an idle frame rather than a stub, and WM_QUIT is delivered when SDL reports the window closed -- without that the game never exits and the port looks hung. GetDC returns NULL, which is what Win32 does on failure and what every caller must check; a non-NULL token would be dereferenced by GDI calls that do not exist. MEASURED: distinct (entry point, module) pairs entered 4207 -> 4958, battery 33/33. The run now gets through the DirectX dialog into shutdown and stops on guest_heap refusing                total        used        free      shared  buff/cache   available
Mem:        16268760     6667164     1328628       28256     8637664     9601596
Swap:       41943032     6612904    35330128 of 0x068bf000, a pointer that did not come from the guest heap.

## What would falsify it

DispatchMessageA does not call the guest's registered WndProc. Nothing here synthesises the messages it would need, and invoking it with an invented one would run game code on an event that did not happen -- but a game whose logic lives in its WndProc would silently do nothing rather than fail, and no run has yet established which this is.
