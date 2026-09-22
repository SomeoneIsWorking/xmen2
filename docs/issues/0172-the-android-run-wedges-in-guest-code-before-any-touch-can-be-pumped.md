---
id: 172
title: the Android run wedges in guest code before any touch can be pumped
status: open
symptom: on the API 35 emulator the run reaches D3D8 device creation and then spins in one compiled block, 98.4% re-entry; SDL is pumped only from PeekMessageA/GetMessageA so no touch can arrive
state_items: S020
tags: android,emulator,wedge,threads,touch,jit
created: 2026-09-19
updated: 2026-09-22
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

## The block, named (2026-09-22)

The engine already tracked the last block entry for `blocks_reentered` and did
not publish it; it does now (`x86p_jit_engine_last_block_entry`, x86port
`3fa2f45`), and the frozen-crossing beat prints it. The block-entry histogram
could never have answered this: it refuses new keys once its table is full, so
a spin that begins after that is absent from it entirely. Armed at 4,096 slots
the wedged run reported **1,761,478,604 of 1,761,605,419 entries dropped** and
a top entry with 11,630 hits at 0.0% -- a confident ranking that did not
contain the spinning block. That report now refuses to present itself as a
ranking when the drops exceed what was kept.

The address is stable across every beat:

```
the primary engine's last block entry was 0x2000e2d5 (unnamed). With 99.6% of
entries re-entering the block just left, that is where this run is looping
```

`0x2000e2d5` is **cg.dll + 0xe2d5** (mapped at 0x20000000, relocated from
0x10000000 -- identically on desktop, so the relocation is not the difference).
Disassembled from the player's own cg.dll, it is the tail of a string-hash
function:

```
  mov  edi, [ecx+0x4]      ; mask
  mov  ecx, [ecx+0x8]      ; bits per step
loop:
  mov  ebx, edi
  sub  esi, ecx            ; esi starts at 32
  and  ebx, edx
  xor  eax, ebx
  shr  edx, cl
  test esi, esi
  jg   loop
```

**The loop terminates by subtracting the step count from 32. A step count of
zero never terminates.** `--set jit.watch=0x2000e2d5` reports the register file
on arrival, and the two hosts disagree exactly there:

| | desktop (reaches gameplay) | API 35 x86_64 emulator (wedges) |
|---|---|---|
| `ecx` — bits per step | `0x0000000a` | `0x00000000` |
| `edi` — mask | `0x000003ff` | `0x00000001` |
| `esi` — counter | `0x16` (32-10) | `0x20`, and it never moves |

So the descriptor at `[esp+0x10]` — a heap object, `0x40000340` in the wedged
run — was built for a 1,024-entry table on desktop and left with a mask of 1
and a step of 0 on the emulator. This is a difference in guest DATA, not in
generated code: the same backend emits the same block on both hosts, and the
block is correct for the desktop operands.

What is still unknown is which earlier call sized that table, and what this
port answers differently there. That is the next step, and it is now a bounded
one.

## Next

1. ~~Resolve `0x000c1060`~~ — it is `memmove`'s own import thunk, not a caller.
2. ~~Name the spinning block~~ — cg.dll + 0xe2d5, a string-hash loop that
   subtracts a step count of zero from 32. See above.
3. Find which call sizes that hash table, and what this port answers
   differently there. The descriptor is a heap object; the mask is 1 rather
   than 1,023, so it was built for a table of two entries. Aim `jit.watch`
   at the constructor once it is identified, and compare the two hosts at
   the same point, which is what settled step 2.
4. The thread question from before stands but is now secondary: the spin is
   not waiting for another thread, it is arithmetic that cannot terminate.
   `0 preemption(s)` on the frozen beat is consistent with that -- no other
   guest thread was waiting for the lock.
