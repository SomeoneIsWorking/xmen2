---
id: 169
title: the import the browser crosses into most is not the one that costs
status: resolved
symptom: ranking imports by call count aims optimization at the wrong one; the most-crossed import is 6.5% of import time
state_items: S021
tags: web,browser,wasm,imports,measurement,instrument
created: 2026-09-19
updated: 2026-09-25
---

# 0169 — the import the browser crosses into most is not the one that costs

- **State items:** S021
- **Status:** the flat `DrawIndexedPrimitive` time is explained (below), a
  larger cost the time table never showed (`GetCursorPos`) is fixed, and
  unchanged uniform pushes no longer re-set their bind group. What is left in
  the draw is ordinary per-draw work, with no single owner.
  This also exists to stop the next optimization being aimed by call count.
- **Found by:** arming the heartbeat's time probe (`--set hotep=64`) on the Dead
  Zone route while looking for the next thing to inline after #167 and #168.

## What the call count says, and why it is wrong

The heartbeat ranks imports by CALLS unless the time probe is armed, and unarmed
it is very persuasive. Over one 5 s interval of the Dead Zone route:

| import | calls |
|---|---|
| `MSVCRT.dll!_ftol` | 90,203 |
| `IDirect3DDevice8!SetTextureStageState` | 72,940 |
| `IDirect3DDevice8!SetRenderState` | 22,671 |
| `MSVCRT.dll!_CIfmod` | 20,800 |
| `IDirect3DDevice8!SetTransform` | 19,498 |

Total crossings that interval: 122,637. **`_ftol` alone is 73.6% of them.** On
this target a crossing is a call out of the translated block's own WebAssembly
module, which #141 measured as the expensive kind, so the obvious next move was
to lower `_ftol` inline and delete three quarters of the crossings.

## What the clock says

Arm the probe and the same table ranks by time. A representative interval, stable
across five consecutive ones:

| import | time | calls | per call |
|---|---|---|---|
| `IDirect3DDevice8!DrawIndexedPrimitive` | **362.0 ms** | 13,621 | 26.6 µs |
| `IDirect3DDevice8!BeginScene` | 80.1 ms | 52 | 1.54 ms |
| `IDirect3DDevice8!Present` | 57.5 ms | 53 | 1.08 ms |
| `MSVCRT.dll!_ftol` | **50.4 ms** | 80,918 | 0.62 µs |
| `MSVCRT.dll!fread` | 46.3 ms | 15 | 3.1 ms |

Host imports cost 779 ms in that interval. **`DrawIndexedPrimitive` is 46% of
it; `_ftol` is 6.5%.** Inlining `_ftol` — the change 73.6% of crossings argued
for — is worth about 1% of wall time. The crossing count was not wrong about
crossings; it was never a statement about time, and nothing in the unarmed table
says so.

`SetTextureStageState` makes the same point harder: 72,940 calls and it does not
appear in the time table at all.

## What the numbers do NOT say

**The split is not a whole-thread profile.** `x86_probe_time_delta` charges
DISPATCHED spans only — host import stubs, and native-override guest bodies,
each exclusive of its children. Translated blocks executing inside
`x86p_jit_engine_run` are in neither. So "host imports 779 ms (45%), guest
bodies 949 ms (55%)" is a split of the 1,728 ms that was dispatched, not of the
5,000 ms interval, and the remaining ~65% is mostly the JIT running translated
code. Reading that remainder as unexplained idle would invent a problem; #162
already owns where the JIT's own time goes.

## The one that is worth looking at

`DrawIndexedPrimitive` at **26.6 µs per call, 276 calls per frame, 6.8 ms of a
94 ms frame**. Two instruments agree on that figure independently: the import
table's 362 ms per 5 s and the renderer's own "host draw 6.59 ms/frame" are the
same measurement from different counters.

Its shape is the interesting part. Across five intervals the call count moved
9,107 → 13,621 while the time stayed pinned at 359.6–362.0 ms. **A cost that
does not move with the call count is not per-call work.** A per-call cost would
have tracked a 50% swing in calls. Candidates, none of them tested: a submission
or fence the path waits on a fixed number of times per frame, a per-frame buffer
map, or a queue that drains at its own rate. Until that is known, "26.6 µs per
draw call" is an average over something that is probably not per-draw, and the
fix cannot be chosen from it.

## What would falsify the ranking

An interval where `_ftol`'s time approaches `DrawIndexedPrimitive`'s, or where
`DrawIndexedPrimitive`'s time tracks its call count proportionally. Neither
happened in five consecutive intervals on this route; a different route with
fewer draws per frame is the honest next check, because every number here is
from one scene.

## Measured again: the flat time is a saturated thread, and the wait was the cursor

2026-09-24, the same `#test-play` route, 10,069,733-byte wasm.
`tools/web_profile_callers.py` reads a saved profile's call tree: who calls
a function, or with `--below`, where the samples beneath one landed.

**The flat time is not a per-frame wait.** Armed again, `DrawIndexedPrimitive`
held 320-401 ms per 5 s while its calls moved 6,168 -> 11,536 *and* the
presents moved 25 -> 44, so it tracks neither calls nor frames. Unarmed, the
V8 profile puts **8.11% of the busiest worker's samples beneath
`dev_DrawIndexedPrimitive`**; that thread is 86-93% busy, and 8% of 5 s is
~400 ms. The import's time is its share of a saturated thread. Scenes with
more draws have cheaper ones. Beneath it the samples are spread:
`d3d8_build_draw_impl` 16%, `gpu_draw` 11%, and the bind-group sets with their
JavaScript crossing (`setBindGroup`, its wrapper, `getJsObject`,
`wasm-to-js`) about 25%. SDL `a42df22` already sets only the groups a draw
changed. What changed them on every draw was the port pushing its vertex and
fragment uniforms on every draw, whether or not they had changed. SDL
`fc0f3c0` (pinned through web-port `42f898f`) now drops a push whose bytes
equal the slot's current data. A temporary counter on this route found
**91.5% of fragment-uniform pushes and 21.4% of vertex slot 0's unchanged**,
so most draws no longer set the fragment uniform group. The canvas reads the
same before and after (mean 32.3-32.4, 91.0% non-black) with no uncaptured
errors.

**The worker's largest wait was the cursor.** The same profile had
`emscripten_futex_wait` at 10.03% of the worker. Half of it (50.5%) was
`GetCursorPos` -> `SDL_GetGlobalMouseState` -> `Emscripten_GetGlobalMouseState`,
a synchronous proxy to the browser's main thread on every call. SDL documents
the function as main-thread-only, and the guest thread was calling it.
`win32_pointer.c` now answers from `SDL_GetMouseState`: the state the guest
thread's own message pump keeps, mapped exactly as `WM_MOUSEMOVE` maps it.
`test_win32_pointer` holds the two equal through a dummy-driver window. It
failed against the old code by hundreds of pixels, (448,216) against (832,504),
because a host with no global cursor position answered a different point than
the message stream. After the change the futex share is **0.66%**, and every
remaining sample of it is OPFS `fread`. The worker went from 85.7% to 93.3%
busy, with translated guest blocks at 59.3% of its samples, up from 51.8%.
The route still presents ~59 a second (293 per 5 s), which is the 60 Hz vsync
cap in this scene, so the gain here is headroom, not frames.

**Also seen, not acted on:** `fread` from OPFS is a synchronous proxy too,
158-339 ms per 5 s over 13-14 calls. That is streaming I/O the guest issues
and waits for, so the fix belongs in how the install is read, not in this
issue.

**The file layer no longer splits guest reads.** `guest_file_io.c` copied every
guest `fread`/`fwrite`/`read`/`write` through a 16 KB stack buffer. That was
left over from when guest pages were separate host allocations. The guest is
now one linear window, so the transfer goes straight through
`guest_memory_pointer`: one host call per guest call. In the browser each host
read is one synchronous OPFS proxy, so a 256 KB guest read was 16 proxies and
is now one. Null ranges and ranges past 4 GB are refused with `EFAULT` before
any transfer; `test_guest_memory_window` covers both, and it fails when the
null refusal is removed. The Dead Zone worker profile afterwards has
`emscripten_futex_wait` at 0.89%, 95.5% of it OPFS reads. That sits inside the
spread of earlier runs on this route (0.66% to 2.45%), which depends on what
the scene streams. No wall-clock gain is claimed from it; the proxy count per
guest read is what changed.

## Resolution (2026-09-25)

The defect was an unarmed heartbeat table that ranked imports by calls and
could be read as a time ranking. Unarmed, the table now heads itself "top
imports by CALLS -- arm X2_HOTEP to rank by time" (`heartbeat.c`). Armed, it
ranks by exclusive host time. The costs that ranking exposed are fixed and
recorded above: `GetCursorPos`'s main-thread proxy, split guest file reads,
and redundant uniform bind-group sets. The remaining `DrawIndexedPrimitive`
time is ordinary per-draw work with no single owner, and the route presents
at the 60 Hz vsync cap.
