---
id: 141
title: engine=jit menu/in-game throughput is capped ~35 FPS by per-import guest<->host crossing cost
status: open
symptom: engine=jit steady-states at ~30-35 FPS at the menu with the frame cap removed; X2_HOTEP wall-time split charges ~70% of the interval to host-import crossings, ~28% to JIT guest bodies; QueryPerformanceCounter is called ~20k times/frame (3.6M per 5s interval), the top import by a wide margin, with _stricmp ~340k, LeaveCriticalSection/WaitForMultipleObjects/ReleaseSemaphore ~100k, floor/_CIfmod ~50k per interval
tags: jit,x86port,performance,crossings,pc,native
created: 2026-09-03
updated: 2026-09-03
---

## Option 1 landed: inline intercept dispatch (2026-09-03)

x86port `d5d3b00` adds `x86p_jit_engine_set_dispatch` -- a between-blocks hook
called *after* the intercept predicate fires. It either services the hand-back
and returns `kX86pDispatchContinue` (the run stays inside `x86p_jit_engine_run`)
or returns `kX86pDispatchUnwind` for the cases that need the interpreter loop's
host frame back (a return to the interpreted call's caller, a setjmp3 thunk, the
engine return trampoline, an address this handler does not own). xmen2 side:
`src/native/x86_engine_dispatch.c` (`x86_engine_jit_dispatch` + the per-thread
`EngineCallCtx` stack), gated by the layered CVar `jit.inline_dispatch`
(default on; `--set jit.inline_dispatch=0` is the A/B).

Measured, driven interactively into a real in-game level (not a script),
identical timed input path, matched wall-time windows:

| metric (45-70s window) | inline_dispatch=0 | inline_dispatch=1 |
|---|---|---|
| host-import share of wall time | 60-64% | 17-19% |
| cumulative frames rendered by 70s | 2725 | 3124 (+14.6%) |
| frame wall avg at 70s | 25.3 ms | 22.1 ms |

The crossing cost is no longer the dominant term in-game: with it removed, the
wall-time split is ~82% guest bodies / ~18% host imports. The remaining gap to
60 FPS is **JIT-translated guest-body execution cost** -- translator quality, a
separate x86port workstream. Per-interval present rate held steady (~12 FPS in
that level, dispatch on or off); the rising *cumulative* frame-wall average is
the fast attract-sequence frames being averaged out, not a regression.

Options 2-4 below remain open and are now the next levers.

## Where the in-game guest time goes (2026-09-03, `jit.profile`)

x86port `787aa3f` adds an execution-weighted block-entry histogram
(`x86p_jit_engine_set_profile`); xmen2 arms it with `--set jit.profile=<slots>`
and prints the top 40 at shutdown. Driven in-game, ~115 s, `X2_UNPACED=1`:

```
1,240,004,563 block entries, 97,568 distinct hot blocks, 0 dropped
 1. 0x2b046cfa  (module)                            3.6%
 2. 0x2d022e1d  (module)                            1.6%
 3-5. 0x2e034d10/44/52  igAttrStack::customReset    2.7% (3 blocks, identical counts)
 8-11. 0x0067217c..0x006721ef  XMen2.exe            2.8% (one tight loop)
19-34. 0x006167a1..0x006169dd  XMen2.exe           ~7%   (~16 blocks, ~4.67M each)
35-36. 0x0055b610/59  frame limiter (issue #35)     0.8% (inflated by UNPACED)
38,40. igWin32LongTimer::getTimeOfDay / igLongTimer::getTimeAsLong  0.6%
```

**The profile is flat.** The hottest single block is 3.6%; the top 40 together
are ~25%. There is no one hot loop to crush -- the game's per-frame guest work
(scene graph, attribute stack, math) is spread across ~100k blocks. `blocks
entered 1.24e9`, `0 fallback steps`, `28,891 insns via helper` of 597k
translated (~5%). So the lever is not coverage (already complete) and not one
override; it is either broad x86port codegen quality (a long grind of better
emitters, measurable now with this profile) or native ownership of localized
XMen2.exe clusters. The two largest such clusters are now natively owned:
0x006167xx/0x006168xx (~7%, IMA ADPCM) and 0x006721xx (~2.8%, `_ftol2`), both
LANDED 2026-09-03 -- see below. The 0x0055b610 frame-limiter spin (option 2) is real but its
count here is inflated by `X2_UNPACED`; collapsing it helps paced CPU/thermal
cost (the Android target) more than uncapped FPS.

### The two localized XMen2.exe clusters, disassembled (2026-09-03)

- **`0x006167a1..0x00616873` (~7%, trip count 4,669,440/run) -- statically-linked
  IMA ADPCM decoders. LANDED 2026-09-03.** `0x00616770` is mono
  (`void(int16_t *out, const uint8_t *in, int count, int *predictor, int *step_index)`,
  `__cdecl`, `eax = count`); `0x00616880` is stereo (low nibble = left, high
  nibble = right, one byte per frame, interleaved output, `eax = out + frames*4`).
  Both are textbook IMA: pre-update `step = kStep[stepIndex]`, advance
  `stepIndex += {-1,-1,-1,-1,2,4,6,8}[nibble & 7]` clamped `[0,88]`,
  `diff = step>>3 (+step,+step>>1,+step>>2 for bits 2,1,0)`, predictor `+/- diff`
  per bit 3 clamped `[-32768,32767]`. The tables at `0x006e95a8` (89-entry step)
  and `0x006e9588` (8-entry index) were dumped from the retail image and match
  the canonical IMA tables byte-for-byte. `src/native/audio_adpcm.c` registers
  native overrides on both; `src/native/audio_adpcm_verify.c` +
  `--set audio.adpcm_verify=1` re-runs the guest body from the same start state
  after every native decode and aborts on any output or state mismatch.
  `test_audio_adpcm` checks both overrides bit-for-bit against an independent
  reference decoder (40 mono samples + 24 stereo frames, state write-back, cdecl
  `esp`). Driven in-game: the override fires on real audio (`audio.adpcm_verify=1`
  reported the first native decode of a 1552-byte mono stream matching the guest
  body, 0 disagreements over the session); with verify off the `0x006167xx` /
  `0x006168xx` blocks leave the `jit.profile` output entirely.
- **`0x0067217c..0x006721f0` (~2.8%) -- statically-linked `_ftol2`. LANDED
  2026-09-03.** MSVC's control-word-independent float->int64 helper
  (`fld st(0); fistp qword; fild; fsubp; add 0x7fffffff/adc`), a pure-x87 leaf
  whose body the JIT ran one instruction at a time through the interpreter
  helper on every conversion. `src/native/crt_static_overrides.c` registers a
  native override on `XMen2.exe!0x0067217c` that reuses `x87_crt_ftol` (the
  imported-`_ftol` implementation -- identical observable contract: pop ST(0),
  truncate toward zero, int64 in EDX:EAX, `__cdecl`). `crt_static_overrides`
  test covers the semantics + registration; a driven in-game session confirms
  the `0x006721xx` blocks leave the execution profile entirely (top block
  count 1.24e9 -> 1.10e9 in a matched window) and gameplay renders correctly.
  The fully-replacing (non-`x86_guest_body`) override contract works: the
  resolver only checks the address is inside the mapped image, and
  `x86_native_call_at` already documents that a hand-written override emulates
  the guest RET itself.

### Profile after both clusters landed (2026-09-03, verify off, ~110 s driven)

```
836,989,734 block entries, 97,758 distinct hot blocks
 1. 0x2b046cfa  libIGGfx  (unnamed)                 4.4%   <- now the single biggest
 3-4. 0x00594524/78  XMen2.exe                      2.5%
 5-7. 0x2e034d10/44/52  igAttrStack::customReset    3.3%
 8-9. 0x006276f6/750  XMen2.exe                     1.6%
10-11,18-20. 0x0055b4xx/0x0055b6xx  frame limiter   1.5%  (inflated by UNPACED, issue #35)
12-17. igWin32LongTimer::getTimeOfDay / igLongTimer::getTimeAsLong  1.5%
25-40. igMemoryPool::getMemoryPoolByIndex / igArenaMemoryPool::isActive / ::contains  ~3%
```
0x006167xx / 0x006721xx are gone -- both clusters are natively owned.

- **`0x2b046cfa` in libIGGfx = `0x10046ce0`, ~4.4%. LANDED 2026-09-03.** A
  shared virtual method of `igDxVertexArray1_1` / `igDx8VertexArray1_1` (same
  vtable slot in both) -- `__thiscall void(Desc *d, int flags)`, `ret 8`. For
  `d->type == 2` it walks vertices `[d->start, d->start+d->count)` and swaps
  bytes 0 and 2 of the 32-bit colour word at `base + stride*i + colour_off`
  (`base = [[this+8]+0x50]`, `stride = [this+0x38]`, `colour_off = [this+0x3b]`),
  keeping bytes 1 and 3 -- a **BGRA<->RGBA channel swap** for D3D vertex colour.
  Then it bumps `[this+0x60]` (dirty) and `[this+0x68]` (lock) per `flags`.
  `src/native/vertex_color_swizzle.c` registers a native override;
  `src/native/vertex_color_swizzle_verify.c` + `--set gfx.vtx_swizzle_verify=1`
  snapshots the vertex span, re-runs the guest body, and aborts on any mismatch.
  `test_vertex_color_swizzle` checks the swap, the flag-bit tail, the non-colour
  type-1 path, and cdecl `esp` against an independent byte-level reference.
  Driven in-game: the override fires (`gfx.vtx_swizzle_verify=1` reported the
  first colour-path swap of a 122,884-byte vertex buffer matching the guest
  body, 0 disagreements); with verify off `0x10046cxx` leaves the `jit.profile`
  output entirely (was profile block #1 at 4.4%).

- **`0x005840a0` in XMen2.exe -- `CDxImmediateBuilder::addVertex`, ~15% of all
  blocks. LANDED 2026-09-03.** The retail 2D/3D immediate-mode vertex sink for
  text, HUD, decals, particles, and immediate geometry (`__thiscall
  void(const igVec3f *pos, const igVec2f *uv, uint32_t col)`, ret 0xc; vtable
  `0x0069c904` slot `+0x0c`). For each vertex it copies Vec3f position, optional
  Vec2f UV (if `[this+0x24] > 0`), and 32-bit color, then advances destination
  pointers by their respective strides. In retail it performed cross-module
  calls into `libIGMath.dll!??4igVec3f` and `??4igVec2f` for every single
  vertex. In a 1000-frame in-game run, this was entered 2,616,028 times (~2,600
  times per frame), producing 6 hot blocks in XMen2.exe plus the two vector
  assignments totaling **20.9 million block entries (15.0% of all JIT
  execution)**.
  `src/native/vertex_builder.c` registers a native override performing direct
  memory copies and stride updates; `src/native/vertex_builder_verify.c` +
  `--set gfx.vtx_builder_verify=1` verifies bit-for-bit equivalence against the
  guest body. Driven in-game for 500 frames with verify active: 0 disagreements.
  With the override active, all 8 top blocks leave `jit.profile` top 40 entirely
  and total block entries dropped from 139.6M to 116.4M (-16.6% across 1000 frames).

`igAttrStack::customReset` is next by size but is a
pure-integer leaf (7 `mov [this+d], imm` stores) that the JIT already
translates -- a native override would add a crossing per call for no
per-instruction saving. The remaining lever is broad x86port codegen quality.


## Where the time goes

Headless `--d3d8 --no-window --set engine=jit --unbounded`, run to steady
state at the attract-loop menu (scenes/presents ~26k), profiled with
`X2_HOTEP=64`:

```
wall-time split this interval: host imports ~3600 ms (72%), guest bodies ~1400 ms (28%)
import KERNEL32.dll!QueryPerformanceCounter: 3,601,309 call(s)   <- one interval
import MSVCR71.dll!_stricmp: 340,863 call(s)
import KERNEL32.dll!WaitForMultipleObjects: 92,973
import KERNEL32.dll!ReleaseSemaphore: 92,974
import KERNEL32.dll!LeaveCriticalSection: ~100-167k
import MSVCRT.dll!floor: 65,536
import MSVCRT.dll!_CIfmod: ~45k
```

~13-24M quantum preemptions/interval at a 20000-crossing quantum.

Caveats on the measurement: `X2_HOTEP` adds a `clock_gettime` pair per
crossing (`span_push`/`span_pop`), so the 72% figure is inflated by the
probe itself. An unprofiled run still steady-states ~35 FPS with host draw
+ upload at ~0.15 ms/frame, so the ~28 ms/frame that is not host render is
guest execution + crossings, and the crossing half of that is the lever.

## Root cause

Every guest->host import call makes `x86p_jit_engine_run` return
`kX86pRunIntercept`, unwinds the JIT slice, and `x2_engine_call` does the
callout (`x2_engine_callout_from/to_x86p` + `x86_dispatch` +
`x86_native_call_at` -> `thunk_call`) before re-entering
`x86p_jit_engine_run`. At ~20k QPC calls/frame that round trip dominates.
QPC is called that often because the game frame-limits / polls the clock in
a spin that the interpreter self-rate-limits and the JIT does not
(the same class as issue #57 / C207, a different spin site: the menu/frame
pacing loop, not libCriMovie).

## Options, roughly in leverage order

1. **x86port: inline intercept dispatch.** Add a consumer callback that
   x86port invokes from *inside* the run loop at an interception point,
   letting the consumer mutate `cpu` (regs/eip/esp) and continue the same
   slice, instead of unwinding `x86p_jit_engine_run` per thunk. Turns N
   thunk calls/slice from N run-function round trips into N direct calls.
   Needs design + tests in the x86port submodule; keep the current
   unwind path as the fallback for overrides/setjmp/return.
2. **Collapse the QPC spin like C207 collapsed the libCriMovie spin.**
   Identify the menu/pacing loop that polls QPC, prove its shape from the
   binary, and give it a bounded wait via a native override, A/B in the
   same binary. Biggest single-site win if the loop is as tight as the
   call count implies.
3. **Cheaper leaf-thunk fast path in `x2_engine_call`.** `_ftol` already
   has an inline path (90acdd0). The pure leaf thunks -- `_stricmp`,
   `floor`, `_CIfmod`, `QueryPerformanceCounter` (its only side effect,
   `winmm_timers_pump`, is callable inline) -- can be handled without the
   `x86_dispatch` + full callout, but doing it per-symbol duplicates each
   implementation; wants a shared "leaf handler" seam, not N `is_X_thunk`
   helpers.
4. **More native ownership** of the frame loop / crit sections so the hot
   guest code between presents is native and never crosses.

Rendering is already cheap at the menu (host draw ~0.1 ms/frame,
upload ~0.04 ms/frame); it is not the in-menu bottleneck. In-game (a real
level, not measurable headless without input scripting yet) will shift the
draw/skinning cost up but the crossing cost scales with it.

## Progress (2026-09-02)

- **Option 3 implemented**: `src/native/engine_leaf_thunks.{c,h}` creates a unified
  leaf thunk dispatch table indexed directly by `(eip - THUNK_BASE) >> 4`.
  Pure leaf thunks execute directly against the `X86pCpu` struct, bypassing
  `x2_engine_callout_from_x86p`, `x86_dispatch`, and `x2_engine_callout_to_x86p`.
  Covered functions: `_ftol`, `_stricmp`, `_strcmpi`, `QueryPerformanceCounter`,
  `QueryPerformanceFrequency`, `toupper`, `tolower`, `strstr`, and `TlsGetValue`.
  Controlled by runtime CVar `engine.leaf_thunks` (default enabled).
  Unit tested in `tests/test_engine_leaf_thunks.c`.
  In a 2000-frame in-game run (`act0/tutorial/tutorial1`), average frame wall time
  improved from 16.89 ms to 14.57 ms (-13.7% overall frame time), with average
  present framerate rising from ~59.2 FPS to ~68.6 FPS.

## Progress (2026-09-04)

- **Option 4 implemented for Scene Graph attribute stack reset (Claim C282)**:
  Profiling with `jit.profile=65536` identified `igAttrStack::customReset` (`0x10034d10`)
  as the hottest block in `libIGSg.dll` (~3.26M entries, 9.8M JIT block transitions,
  ~2.4% of total guest execution time), called in an inner loop by
  `igAttrStackManager::reset` (`0x10034d30`).
  Implemented native overrides for `0x10034d10` and `0x10034d30` in `src/native/attr_stack.{c,h}`,
  running the attribute stack loop natively in host C, executing `clearLightHandles`
  via `x86_guest_call_args`, and correctly popping the return address (`C->esp += 4u`).
  Added runtime CVar `sg.attr_stack` (default enabled) in `src/config/runtime_cvars.cpp`.
  Added differential verification harness in `src/native/attr_stack_verify.{c,h}`
  (`sg.attr_stack_verify=1`), verified over 1,740 in-game frames with 0 divergences.
  Added unit test suite in `tests/test_attr_stack.c` (test #76).
  In a 2000-frame in-game benchmark (`act0/tutorial/tutorial1`, `X2_UNPACED=1`):
  Average frame time reduced from 16.62 ms to 15.14 ms (-8.9%), with present
  framerate increasing from 57.4 FPS to 62.7 FPS (+9.2%).

- **Fast-path Meyers singleton getter for timer (Claim C283)**:
  `XMen2.exe!0x0055b610` was entered ~2.8M times per 2000 frames to retrieve the
  global timer singleton (`0x007ac248`). Each call previously set up and tore down
  a full MSVC SEH exception frame via `x86_guest_body`.
  In `src/native/startup.c`, `x2_override_0055b610` now directly returns `0x007ac248`
  in `C->eax` and pops `ret` (`C->esp += 4u`) once the guard byte is set, skipping
  SEH construction and engine re-entry.
  Average frame time dropped further to 14.65 ms, with framerate rising to 64.7 FPS
  (2000 frames completed in 30.95s vs 34.95s originally, a cumulative +12.7% FPS improvement).
