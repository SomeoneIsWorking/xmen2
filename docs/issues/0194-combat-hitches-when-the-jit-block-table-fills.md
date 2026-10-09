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

The limits are ceilings now, not allocations (jit-common `8a0e953`, x86port
`ff72f97`): the block table starts at 8,192 entries and doubles as blocks
arrive, chain slots are initialised as they are claimed, and native code memory
is made in chunks (a sixteenth of the ceiling, at most 32 MB) as the working set
reaches them. Only reaching a ceiling flushes. Native ceilings are 2,097,152
blocks and 512 MB; `jit.blocks` / `jit.code_mb` still override them.

After the change the same route translates 91,582 blocks into 31.6 MB in one
chunk with no flush. The heartbeat's arena line names chunks and whole-cache
flushes, because it used to say the arena held the working set while it was
flushing.

Frame-time percentiles in the fight window vary more between runs than between
configurations, so no steady-state gain is claimed. The phone is not measured.
