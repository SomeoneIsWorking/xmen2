---
id: 194
title: Combat hitches for seconds when the JIT block table fills
status: resolved
symptom: Combat is reported slow on the phone; on desktop a Danger Room boss fight showed 1.4-1.8 s frames, p99 212 ms
state_items: S018
tags: performance,jit
created: 2026-10-08
updated: 2026-10-08
---

# 0194 — Combat hitches for seconds when the JIT block table fills

## Reproduction

`tools/live_case.py combat-bench`: the retail autosave Continues, `loadmap
dr/dr_boss1`, the party walks six steps into Lady Deathstrike and fights for
about 18 s. The case fails if the engine's shutdown summary counts a flush.

## Cause

`src/native/x86_engine_jit_pool.cpp` sized the native engine at 65,536 blocks.
The native code storage is bounded only by bytes and cannot evict a block, so
when the block table or the chain-slot pool (both sized by that count) fills,
`x86p_jit_engine_invalidate_all` drops every translation and the working set is
translated again. Boot to Sanctuary alone reaches about 62,000 blocks; Sanctuary
plus the boss fight needs 91,356 blocks and 31.6 MB of code.

Measured 2026-10-08 (desktop, unbounded, same route):

| Blocks | Flushes | Translated | Worst frames |
|---|---|---|---|
| 65,536 | 1-2 | 125,000-160,939 | 1,772 ms and 1,376 ms when the flush landed in the fight |
| 262,144 | 0 | 91,153-91,542 | no flush-driven frames |

A perf profile of the flushing run put 21% of all samples in
`x86p_jit_translate_bounded`.

## Fix

Native default raised to 262,144 blocks and 128 MB of code: about 2.9 times
this route's working set, 25 MB of table and chain slots for the single native
engine. `jit.blocks` / `jit.code_mb` still override both.

Frame-time percentiles in the fight window vary more between runs of one size
(p50 6.8 to 15.9 ms) than between sizes, so no steady-state gain is claimed.
The phone is not measured; it uses the same default.
