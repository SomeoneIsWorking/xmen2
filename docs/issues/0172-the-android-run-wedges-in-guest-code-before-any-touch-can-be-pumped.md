# 0172 — the Android run wedges in guest code before any touch can be pumped

State items: S020 (platform-neutral touch play)
Status: open. The touch chain itself is not implicated; nothing can reach it.

## Symptom

On the API 35 x86_64 emulator, launched straight into `act0/tutorial/tutorial1`
through the debug private-install route, the run reaches D3D8 device creation
and then stops making progress. Every beat afterwards:

```
[HB]   20.0s  the guest crossed the host boundary NOT ONCE in the last 5.0s
              (crossings unchanged at 25154).
[engine] [HB] JIT: 570985925 blocks entered (561991752 re-entered the block
              just left, 98.4%), 20783 translated
[touch] [HB] no contact reached the port this run -- touch_controls=AUTO,
             source says not touch, gate never-seen, a window was present.
             Nothing was dropped; nothing arrived
```

The two lines together say what neither says alone: the guest is **not**
stopped. It is spinning inside one compiled block, 98.4% of block entries
re-entering the block just left, and a guest-to-guest branch crosses no host
boundary, so the crossing counter cannot see it.

## Why touch is silent, and why that is not a touch defect

`adb shell input tap` was dispatched 25 times over the canvas. Not one contact
reached the port. SDL is pumped only from `imp_USER32_PeekMessageA` /
`imp_USER32_GetMessageA` (`src/native/win32_events.c:249,263`), which the guest
calls from its message loop. A guest that never leaves a spin never calls
either, so every finger event stays in SDL's queue.

Touch activation is already platform-neutral: `x2_touch_source_note`
(`src/input/touch_source.cpp`) sets the source to touch on the first
`SDL_EVENT_FINGER_*` and there is no `__ANDROID__` or `__EMSCRIPTEN__`
conditional anywhere in `src/input/`. The same code publishes to the pad in a
browser (issue #171). Android is blocked upstream of it.

## What the ring says

The boundary ring's tail — reachable on this path only since the heartbeat
stopped skipping its own dump on a frozen-crossing beat — is 96 crossings of
one repeating shape:

```
[TRACE]   memmove                esp 700ffc98 -> 700ffc9c  (+4)
0x000c1060 (no registered module)
[TRACE]   memmove                esp 700ffc8c -> 700ffc90  (+4)
0x000c1060 (no registered module)
[TRACE]   memmove                esp 700ffc80 -> 700ffc84  (+4)
0x000c1060 (no registered module)
```

Three `memmove` calls per iteration from `0x000c1060`, which is in no mapped
module: XMen2.exe is at 0x00400000 and every DLL at 0x10000000 or above. That
caller is the next thing to identify.

## Not reproduced elsewhere

The same revision reaches gameplay on desktop and in the browser. Whether the
spin is Android-specific or a timing window that the emulator's scheduling
makes reliable is unknown; no desktop run has shown it.

## Next

1. Resolve `0x000c1060` — generated thunk, copied code, or a corrupted return.
2. Establish which guest thread is spinning and what it is waiting for. The
   interval's top imports before the freeze were `WaitForMultipleObjects`
   (9857 calls) and `ReleaseSemaphore` (9858), so a worker handshake is the
   first place to look.
