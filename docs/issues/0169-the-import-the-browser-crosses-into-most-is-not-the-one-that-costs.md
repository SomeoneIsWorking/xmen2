# 0169 — the import the browser crosses into most is not the one that costs

- **State items:** S021
- **Status:** measured; no fix attempted. This exists to stop the next
  optimization being aimed by call count.
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
