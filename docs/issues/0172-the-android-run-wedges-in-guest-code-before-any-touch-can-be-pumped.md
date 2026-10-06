---
id: 172
title: the Android run wedges in guest code before any touch can be pumped
status: resolved
symptom: on the API 35 emulator the run reaches D3D8 device creation and then spins in one compiled block, 98.4% re-entry; SDL is pumped only from PeekMessageA/GetMessageA so no touch can arrive
state_items: S020
tags: android,emulator,wedge,threads,touch,jit
created: 2026-09-19
updated: 2026-09-24
---

# 0172 — the Android run wedges in guest code before any touch can be pumped

State items: S020 (platform-neutral touch play)
Status: resolved by x86port `80e454a` (see "The cause" below). The touch chain was never implicated.

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
| `edx` — the hash so far | `0` | `0x361` |

**These are each host's FIRST arrival at that block, not the same lookup** —
the differing `edx` says the strings differ — so the table is not the desktop
run's same table gone wrong. What the pair does establish is the shape of the
defect: the loop is correct and terminates for a table whose step is 10, and
on the emulator it is handed one whose step is 0 and cannot. The descriptor is
a heap object (`0x40000340` in the wedged run), so this is a difference in
guest DATA, not in generated code: the same backend emits the same block on
both hosts.

The string being looked up is named on the stack: `cg.dll + 0x79494`, which in
the file is the ASCII `"texture unit 0"`. So this is a Cg parameter lookup, and
the table it searches has room for two entries — consistent with a Cg program
whose parameters were never populated.

Two constructors write these four fields, and neither fired under a watch
during the wedged run:

* `cg.dll + 0xdc20` sizes the table properly. It clamps the requested count to
  at least 2 and then computes the bit count **in x87** — `fldln2`, `fild`,
  `fyl2x`, a second `fyl2x` against a constant, `fdivp`, i.e. `log2(count)` —
  before converting to an integer and storing it at `+8`. A zero out of that
  sequence is exactly the observed state.
* `cg.dll + 0xe1f0` writes `+4` and `+8` as literal zero.

So the next step is to find which one built THIS table and with what argument.
Neither entry address was reached while the watch was armed, which means the
table predates engine-diagnostic setup or a third path builds it.

## The table, read (2026-09-22)

`jit.peek` dumps guest memory beside a `jit.watch` report, so the descriptor
itself is readable now. At the wedge, 0x40000340 holds:

```
+00 00000001   size      (1 bucket)
+04 00000001   mask
+08 00000000   bits per step   <-- the zero
+0c 40000330   the bucket array
```

The constructor at `cg.dll + 0xdc20`, read from the player's own DLL rather
than paraphrased, makes all four from one number:

```
  ecx = arg1                     ; the requested entry count
  if (ecx < 2) arg1 = 2          ; clamped, so log2 of it is never below 1
  fldln2 / fild arg1 / fyl2x     ; ln(count)
  fldln2 / fld qword 0x10072188  ; that constant is 2.0
  fyl2x / fdivp                  ; ln(count)/ln(2) = log2(count)
  call 0x1000e0b0                ; -> ceil
  call 0x10068ca0                ; -> _ftol: FNSTCW, OR AH,0Ch, FLDCW, FISTP
  [esi+8] = eax                  ; bits
  [esi+4] = (1<<bits)-1          ; mask, built a bit at a time
  [esi]   = 1<<bits              ; size
```

**The observed state is not reachable from that code.** `bits = 0` gives
`size = 1` and `mask = 0`, and the mask here is 1. A `bits` of 0 beside a mask
of 1 is not any output of this constructor, for any argument.

And `--set jit.watch=0x2000dc20` **never fired** across a full wedged run, so
this constructor is not merely producing the wrong answer -- it is not running
at all. Something else writes those fields. Finding it is the next step, and
the tool below is what it needs.

## The write watch cannot answer this (2026-09-22)

The previous Next said `write_watch=0x40000348:0` would name the writer. It
cannot, and this is the second time the issue has planned around it.

`x2_write_watch_fire` is called from `src/runtime/x86_abi/x86rt.h` -- the WR8/
WR16/WR32 macros -- which are the accessors **hand-written native overrides**
use, plus the CRT's `memcpy`/`memmove` destinations. JIT-translated guest code
stores straight to the mapped page and calls nothing. So the watch sees writes
made by this port's own C and is blind to every write the guest makes, which is
the only kind this question is about. Armed at `0x40000348:0` through the
runtime conf on the wedged emulator run it reported, correctly and uselessly,
nothing.

It is now armable where the bug reproduces at least: `write_watch` and
`guest_watch` are registered CVars rather than `getenv` reads, so the Android
runtime conf reaches them. That removes the packaging blocker and leaves the
real one.

**What is needed is a guest-store watch in the JIT**: an address compared in
emitted store code, with translations flushed when it is armed so an unarmed
run emits no compare at all. That belongs in x86port beside the block cache,
and it is the tool this issue has now been blocked on twice.

## The cause: x87 memory operands read as the wrong format (2026-09-24)

The emulator is x86_64, and Bionic's x86_64 `long double` is IEEE binary128
-- the only x86-64 host where it is not the ext80 object. The x64 backend's
x87 slow paths widen a memory operand with the host's own `fld` and then
`fstp tbyte` into a stack scratch, and handed that scratch to adapters typed
`const long double *`. On binary128 that reads ten ext80 bytes (plus six of
stack garbage) as a binary128 value, so every JIT-translated FLD m32/m64,
FILD, memory FADD/FSUB/FMUL/FDIV and FCOM m32/m64 took a wrong operand on
this host and on no other. The constructor at `cg.dll + 0xdc20` is exactly
that code -- `fild`, `fld qword`, `fyl2x`, `fdivp` -- so its bits-per-step
can come out as anything, including the zero the hash loop cannot survive.
Why `jit.watch` on its entry never fired is not yet explained.

x86port `80e454a` decodes the scratch through `x86p_x87_from_f80`. Its new
`tools/verify.py --binary128-model` builds the whole framework with
`-mlong-double-128` on Linux and is a CI step; reverting the fix there fails
seven suites. The same model found and fixed MMX being refused, register
padding left nondeterministic, and software FXTRACT missing on every non-x87
host.

## Verified on the emulator (2026-09-24)

The x86_64 debug APK at the new pin, private-install route, booted straight
into `act0/tutorial/tutorial1` on `codex_shared_api35`. At 60 s host-boundary
crossings were still climbing (+84,248 per beat), 135,387 GPU draws had been
made, and `jit.watch=0x2000dc20` now fires: the constructor runs. The screen
showed the tutorial's opening dialogue, and one `adb shell input tap` on
CONTINUE advanced it ("2 contact(s) became the retail GUI pointer"), so touch
reaches the game on Android.

The guest-store watch this issue planned for was not needed and was not built.
