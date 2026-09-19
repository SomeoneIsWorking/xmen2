---
id: 165
title: frustum culling is a fifth of every block the browser enters
status: open
symptom: the hot-block histogram attributes a fifth of block entries to the engine's frustum cull; no native override is written
state_items: S021
tags: web,browser,wasm,jit,override,performance
created: 2026-09-19
updated: 2026-09-19
---

# 0165 — frustum culling is a fifth of every block the browser enters

- **State items:** S021
- **Status:** located and measured; no override written yet
- **Found by:** the hot-block histogram, once it printed from the heartbeat
  (see the report change that made it readable on a target with no shutdown)

## The measurement

`jit.profile=65536` over a Dead Zone route in the browser: 61,222 distinct
blocks, 730,291,642 entries, **0 keys dropped**, so the whole table is
trustworthy and not just its head.

The row-by-row view says there is nothing to fix. The hottest single block is
2.7% and the other thirty-nine are at or under 1.1% — the flat profile #141
reported on the desktop, confirmed on a second target.

The clusters say otherwise. Eighteen blocks between `0x2e047470` and
`0x2e04861e` carry **5,617,730 entries each, to the entry**, which is what a
straight-line path through one call tree looks like when every block on it runs
exactly once per call.

`libIGSg.dll` is mapped at `0x2e000000` in this run — fixed by matching the
named block `0x2e0485b0` against the export table of the player's own DLL,
where `?igFrustCullNode@Sg@Gap@@YA?AW4IG_TRAVERSAL_RETURN@12@PAVigTraversal@12@PAVigObject@Core@2@@Z`
sits at RVA `0x000485b0`. Every hot address in the cluster is therefore an RVA
in `libIGSg.dll`, and the call tree disassembles cleanly:

| RVA | what | entries |
|---|---|---|
| `0x0485b0` | `igFrustCullNode`, exported, the traversal callback | 5,617,730 |
| `0x047470` | private; the cull test it calls | 5,617,730 |
| `0x047570` | private; called by `0x047470` | 5,617,730 |
| `0x0478e0` | private; called by `0x047470` | 5,617,730 |
| `0x0484c0` | private; the child dispatch `igFrustCullNode` calls after it | 5,617,730 |
| `0x047972` | private; a second path, on some nodes only | 3,649,212 |

`igFrustCullNode` at 5,617,730 calls over 1,810 presented frames is about 3,100
scene-graph nodes tested per frame.

**The share has to be taken over a window, not over the run.** The histogram is
cumulative from boot, and boot is a different program: the cluster is not in the
top forty at all for the first two heartbeats, and its cumulative share climbs
from 0.44% per block to 0.76% per block and is still climbing at the end.
Differencing two snapshots a minute apart, over a window of 216,211,341 block
entries, the eighteen blocks come to **22.5% of every block entered** -- half
again what the cumulative table reads. Every share below is windowed for the
same reason.

Note what `computeCompositeMatrix` is NOT. It is exported at `0x047440` and it
looks like the obvious suspect, but it disassembles to eleven instructions that
forward to an import and store two pointers, ending in `ret $0x8` at
`0x04746a`. The hot body is the unexported function that begins six bytes
later. Reading the cluster as "the nearest export" would have named the wrong
function.

## Why this is the override the route wants

The port's architecture answers hot guest code with a native override, and the
two claims that already did it on this title measured what it is worth: claim
280's `CDXImmediateBuilder::AddVertex` cluster was 15.0% of JIT execution and
its override removed 16.6% of all block entries in a matched run. This cluster
is the same size and has one exported entry point, which is what an override
needs.

It is also where the x87 cost lives. A frustum test is transform and compare on
floats, and on this host every one of those is Bochs softfloat in an 80-bit
format the machine does not have — issue #162 measures x87 at roughly 38% of
the guest worker after the memory and LTO work. Culling does not stop being
work when it becomes native, but it stops being *emulated* work.

## The second cluster is not culling, and it is not x87 either

`0x2d022e1d` is the hottest single block in the run at 3.42% of the window, and
the three addresses around it add 3.4% more. `libIGMath.dll` is at `0x2d000000`
-- fixed the same way, from `igAABox::getMeta` at RVA `0x025b80` -- and RVA
`0x022e1d` is unexported, in the gap between `lerp@igVec4uc` at `0x022cd0` and
`getFirst` at `0x0230b0`.

It disassembles to a matrix-palette skinning loop, and it is already SSE:
`movaps` of four matrix rows indexed by a bone byte, three `mulps` and three
`addps` to transform, a `movss`/`shufps` to broadcast the weight, and `loop`
back to the top. Nothing about it is emulated floating point, so nothing about
it is the x87 problem.

It is hot for a structural reason instead. `LOOP` terminates a basic block, so
every iteration -- every bone of every vertex -- leaves the translated code,
returns to `x86p_jit_engine_run`, pays the intercept callback and the block
cache lookup, and re-enters through the table, to run fourteen SSE
instructions. That is a dispatch per bone. A backend that lowered a branch back
to a block's own entry as a wasm `loop` instead of an exit would collapse it,
and would do the same for every guest loop; it would need a bounded iteration
count so the engine's step budget and its preemption still work. That is its
own issue, and a smaller one: dispatch is about 12% of the guest worker in
total, so it is the ceiling on that change, while this one removes the work as
well as the dispatch.

## What makes it harder than claim 280's

`igFrustCullNode` is not a leaf. Between `0x0485b0` and `0x048625` it makes a
virtual call through `*0x50(%edx)`, writes a ±FLT_MAX box into the traversal at
`0x1d8`, calls an import at `*0x10070978`, and hands off to `0x0484c0`, which
dispatches through a jump table at `0x10048590` and then calls `*%ebp` and
`*%ecx` — the child callbacks. A native override has to call back into guest
code for those, so the work is the call-out contract as much as the math.

## What would falsify the attribution

One route, one map, one camera. If a window taken elsewhere — a cutscene, a
menu, a different act — puts this cluster well under 20%, then it is the cost
of the Dead Zone's scene graph and not the game's, and the override is worth
proportionally less. A second route's histogram is a cheap check now that the
heartbeat prints one, and it should be taken before the RE starts.

The narrower falsifier is the base address. Everything above depends on
`libIGSg.dll` being at `0x2e000000` in that run, which was fixed from a single
matching export RVA. If another run maps it elsewhere and the same RVAs do not
come back hot, the mapping was coincidence.

## The falsifier this file asked for, answered -- and it holds

Twelve consecutive five-second windows, differenced with the new
`tools/web_hotblocks.py`:

| range | what | median share of block entries |
|---|---|---|
| `libIGSg 0x047470-0x04861e` | frustum culling | **21.25%** (min 20.81, max 21.72) |
| `libIGSg 0x063400-0x0635ff` | `igTraversal::dispatch` and neighbours | 5.25% |
| `libIGMath 0x022e00-0x022eff` | SSE matrix-palette skinning | 6.83% |
| `libIGGfx 0x04a180-0x04a1ff` | — | 3.71% |

Stronger than a second map would have been, as it happens. Across those twelve
windows the machine took about half the worker away, so total entries per window
fell from 18.5M to 9.8M -- and the culling share did not move by one point. It
is a fixed proportion of the traversal, not an artefact of where the camera was
standing. The headline figure is 21.25% and not the 22.5% written above, which
came from a single differenced pair.

Split by call-tree part: `0x047470` alone is 7.79%, `0x047570` 3.89%,
`0x0478e0` 2.83%, `0x0484c0` 6.75%.

## Why this is nonetheless NOT the next thing to do

Block entries are not time, and this file's title is about entries.

A census of the guest worker's *samples* puts all translated guest code at 21.9%
and dispatch at 12.4%, against 39.1% for x87 emulation (issue #162). Twenty-one
percent of block entries is therefore something on the order of seven percent of
the worker, plus whatever x87 the subtree drives -- `0x047470` computes its box
extents with `fld`/`fsub`/`fstp`, so some of the 39% is its. Call it under a
tenth, against an RE unit that has to recover four private helpers and a
call-out contract for a virtual call, an import and two child callbacks.

**The ranking in this file was built on the wrong denominator.** Entries are the
right unit for finding a cluster and the wrong one for sizing the work to remove
it. x87 is 39% of the worker and title-neutral; it goes first.

What would make this file's work worth starting again: an attribution of x87 and
dispatch time to the guest blocks that caused it, from the profile's call tree
rather than from self time. If the culling subtree's inclusive cost turns out to
be a fifth of the worker rather than a fifteenth, this ranking flips back.
