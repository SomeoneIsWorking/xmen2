---
id: 177
title: The JIT room check counted modules, so a full block-record table aborted the run
status: resolved
symptom: "native code called abort() shortly after a menu selection, with `all 65536 block record(s) are live` in the console"
state_items: S021
tags: web,browser,jit,x86port,storage,eviction
created: 2026-09-20
updated: 2026-09-20
---

# 0177 — the room check counted modules and the run aborted on block records

State item: S021 (web product)
Status: FIXED in x86port `e2c4ca9`, pinned here.

## What a player saw

"The game stopped: native code called abort()". Reproduced in Zen (Firefox 156)
on the retail `#play` route: the menu rendered, a selection was made, the level
began loading, and the run died. The console said:

```
[engine:error] all 65536 block record(s) are live; entry point 0x004612e0 (unnamed), at 0x0056b18c (unnamed)
```

## What it was

The WebAssembly storage bounds two different resources. A translation is filed
in a BLOCK RECORD, and records are published through engine MODULES. Compaction
gathers up to thirty-two blocks into one module, so the two counts diverge by
about that factor as soon as a run is warm.

`x86p_jit_storage_room()` asked the arena how many MODULES were live and
compared that with the BLOCK RECORD capacity. With 65,536 records the module
count tops out near 2,000, so the comparison never fired: `room()` answered
"there is room" while every record was taken.

Everything downstream then did exactly what it was told. `evict_for_room()` in
`jit_engine.c` loops only while `room()` says there is none, so it evicted
nothing. `take_block_slot()` scanned the record array, found no free slot, and
returned the refusal above. The one retry above it is deliberate — "a second
refusal after eviction is a host that cannot hold one block" — so the run
returned `kX86pRunOutOfCode`, this port's `refuse()` dumped and called
`abort()`, and the player saw the game stop.

It is not a browser limit, not memory pressure (that is #176) and not the
learned live-module ceiling: the storage had thousands of free module slots at
the moment it refused.

## The fix

The storage counts its own records. `live_blocks` rises where a record is
actually made live, in `x86p_jit_storage_translate` after the record is
written — not where a free slot is found, because the translation and
publication steps between those two points return without publishing, and a
count raised for a record that never became live never comes back down. It
falls in `drop_block` and is zeroed by `x86p_jit_storage_reset`.
`x86p_jit_storage_room()` reads it.

`tests/test_jit_storage_wasm.c` in x86port compiles the shipping storage with a
stub wasm host, fills every record, and checks the answer: it reports
`kX86pJitStorageOutOfSlots` now and reported `kX86pJitStorageRoom` before, which
is the check that fails on the old comparison.

## What it bought

Measured on the same Zen retail route, driven from the menu into a level with a
real key press through `WebDriver:PerformActions`: the run no longer aborts, and
**the retail route reaches gameplay in a browser for the first time** — a level
rendering continuously with its HUD, 2,063 presents over 265 s. The heartbeat
now attributes evictions to the limit that asked for them: `252 the module
slots, 133 the live-module ceiling`, where the record path had previously never
asked at all.

The frame rate on that route is 124.5 ms a frame, of which 49.3 ms is the
swapchain wait. That is a separate problem and stays open.
