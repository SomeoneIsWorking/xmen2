---
id: 172
title: the Android run wedges in guest code before any touch can be pumped
status: open
symptom: on the API 35 emulator the run reaches D3D8 device creation and then spins in one compiled block, 98.4% re-entry; SDL is pumped only from PeekMessageA/GetMessageA so no touch can arrive
state_items: S020
tags: android,emulator,wedge,threads,touch,jit
created: 2026-09-19
updated: 2026-09-19
---

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

**That address is not a caller, and this reading was wrong.** `0x000c1060` is
in the host-import thunk range: `THUNK_BASE` is `0x000C0000` and each thunk is
16 bytes (`src/native/x86rt_native.h:53`), so `0x000c1060` is thunk index 262 --
the synthetic address of `memmove` **itself**, which is what an import crossing
records. The ring's address field for an import crossing is the import, not
the code that called it.

The dump said "(no registered module)", which is true and useless, and it is
what sent the previous step looking for a caller in copied or corrupted code.
It now names the thunk instead:

```
0x000c1060 (the host import thunk for <module>!memmove -- not a caller)
```

So the tail says only that the spinning code calls `memmove` three times per
iteration. The caller is still unidentified, and the ring cannot identify it:
`ret` is recorded as 0 on the import path.

## Not reproduced elsewhere

The same revision reaches gameplay on desktop and in the browser. Whether the
spin is Android-specific or a timing window that the emulator's scheduling
makes reliable is unknown; no desktop run has shown it.

## Next

1. ~~Resolve `0x000c1060`~~ — done: it is `memmove`'s own import thunk, not a
   caller. What is still missing is the guest code that calls it, which the
   ring cannot give because it records no return address for an import
   crossing. `--set jit.watch=<addr>` (issue #158) reports the block just left
   and the register file for a named guest address, which is the tool for this
   once the spinning block's address is known from `jit.profile`.
2. Establish which guest thread is spinning and what it is waiting for. The
   interval's top imports before the freeze were `WaitForMultipleObjects`
   (9857 calls) and `ReleaseSemaphore` (9858), so a worker handshake is the
   first place to look.
