---
id: C125
kind: claim
status: holds
created: 2026-08-06
tags: graphics,vulkan,ark,rc-exe
---

## Claim

The renderer's display initialisation SUCCEEDS: the engine opens the display through igVkVisualContext and the run reaches the Cg shader loader

## Evidence

The blocker was not the renderer at all -- it was USER32::GetDC returning NULL. igWin32Window::open (libIGDisplay 0x10005740) calls CreateWindowExA then GetDC and treats a NULL DC as fatal (CALL [0x100090d8]; TEST EBP,EBP; JZ -> return false), and that false latched the game's startup error byte and surfaced four hops later as 'Display failed!'. Confirmed at runtime with X2_ARGS: open(ecx=0x01e08f28, "X-Men Legends 2", 0x320, 0x258) returned eax=0, returning to 0x005faf4b which is exactly the TEST AL,AL identified statically.

GetDC now returns a token for the main window. That is safe because the ENTIRE GDI surface this game imports is one function -- GetDeviceCaps, measured across the module's imports -- and it does not look at the HDC at all.

With that, the engine drives the renderer through display init: setVideoMode, setNativeWindowHandle, and 'igVk: swapchain claimed on window 0x...' -- the first time a swapchain has been attached to the GUEST's window rather than a test's. Slots 38 (setNativeDeviceHandle, super-called), 30 (open, transcribed with only createDevice replaced) and 25 (detectDriverDatabaseProperties) were then demanded in turn by the engine and implemented, and the run now reaches LoadLibraryA("cg.dll") -- the NVIDIA Cg shader path of C112, which is well past display initialisation. No 'Display failed!', no unimplemented-slot abort, no SIGSEGV.

## What would falsify it

A frame. 0 frames are still presented -- the engine has not reached beginDraw -- so 'display initialisation succeeds' is claimed from the engine's own progress past open() and NOT from anything appearing on screen. If it turns out the engine is proceeding on a display it considers half-open, this overstates it.
