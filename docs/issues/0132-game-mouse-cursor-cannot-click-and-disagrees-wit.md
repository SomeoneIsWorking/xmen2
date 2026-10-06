---
id: 132
title: Game mouse cursor cannot click and disagrees with OS cursor
status: resolved
symptom: The game-drawn cursor does not activate UI controls, is not aligned with the visible OS cursor, and both cursors are shown
state_items: S003,S006
tags: mouse,cursor,input,presentation,ui,user-report
created: 2026-08-27
updated: 2026-08-28
---

## Root cause

`pump_sdl` drains SDL mouse motion/button events but drops every event not
captured by RmlUi. The Win32 compatibility surface cannot deliver them later:
`PeekMessageA` and `GetMessageA` implement only `WM_QUIT`, while
`DispatchMessageA` is a no-op even though `RegisterClassA` retains the guest
WndProc.

That bypasses the actual Alchemy GUI input owner. `igWin32Window::getEvents`
(`libIGDisplay` `0x10006430`) runs the ordinary Peek/Get/Translate/Dispatch
loop, and `dispatchEvent` (`0x10006550`) consumes `WM_MOUSEMOVE` and the mouse
button messages to update `igMouse` and call the interface manager. DirectInput
relative mouse state is a separate title input-binding path, so keeping it
alive cannot move or click the retail GUI cursor. The visible SDL cursor is a
second, independently controlled pointer, which is why it can disagree with
the game-drawn one.

## What was tried / dead ends

Issue #92 corrected the rendered XYZRHW cursor's vertical geometry. It did not
claim to route mouse input, and the present observation does not falsify that
fix. Applying another Y flip would be wrong: the retained Alchemy dispatch
already converts Windows top-down client Y into its own coordinate space.

## Resolution

`win32_events` now owns the SDL-to-Win32 message boundary used by the retained
Alchemy event loop. It translates SDL motion and left/right/middle buttons into
ordered `WM_MOUSE*` messages, implements filtered remove/no-remove
`PeekMessageA` and blocking `GetMessageA`, and calls the registered guest
WndProc through the checked stdcall bridge. Adjacent motion is coalesced like
Win32, while queue overflow refuses to discard a button or activation event.
RmlUi-consumed input does not click through to the game.

`win32_mouse` maps physical window coordinates through the inverse of the
presenter's aspect-fit rectangle into the active logical D3D backbuffer. It
does not add a second Y inversion: the retained Alchemy dispatch owns its
Windows-to-engine conversion. The same owner arbitrates one visible pointer:
the OS cursor is hidden while focused retail content displays the game cursor,
and restored outside the window or while a native overlay/modal is active.
Hidden test windows never change desktop cursor state. `test_win32_mouse`
pins message ordering/filtering, coordinate clamping/aspect mapping, button
state, motion coalescing, and cursor visibility policy.

The visible `mouse-click` case passed 3/3 under an isolated X11 desktop: a
physical window click on NEW GAME changed the retail frame and opened the
game's difficulty dialog. That exercises the real X11 -> SDL -> Win32 message
queue -> retained WndProc path rather than a scripted keyboard substitute.
