---
id: 153
title: The wasm JIT held 1024 modules whatever cache it was asked for, so the browser retranslated 7,244 blocks per second
status: resolved
symptom: browser JIT reports millions of translations and only 1.6 MB of live code; 42.7% of the busy worker's CPU samples are in `new WebAssembly.Module`/`Instance`
tags: web,browser,wasm,jit,x86port,performance
created: 2026-09-19
updated: 2026-09-19
---

## Symptom

On the Dead Zone route in headless Chrome, the heartbeat reported a translation
rate that never fell:

    [HB] JIT: 149763267 blocks entered, 4916312 translated (47502792 instructions);
         0 refusals of 4916312 translation attempts; 0 cache flushes, 1486677 bytes code
    [HB] JIT: 151014441 blocks entered, 4967649 translated (48008313 instructions);
         0 refusals of 4967649 translation attempts; 0 cache flushes, 1613695 bytes code

Ten seconds apart: **51,337 translations, 5,106 per second**, against 124,447
blocks entered per second. One translation for every 24 block entries, forever.
A warm JIT translates nothing.

## Cause

`X86P_WASM_MAX_LIVE_MODULES` in x86port's `jit_wasm_arena.h` was a fixed 1024.
`x86p_jit_storage_create()` took only a byte capacity, so the module cap was
invisible to the caller and never agreed with the block cache the engine was
given. `src/native/x86_engine_jit_pool.c` asks for `kCacheBlocks = 8192`, so
the browser ran with 8,192 cache entries backed by room for 1,024 translations:
seven eighths of the cache could not hold anything, and past the thousandth
block every translation released a module the cache still named — a cache hit
whose code was gone, which is a miss with extra steps.

The `1613695 bytes code` above is the proof it was the module count and not the
byte budget: 1,613,695 / 1024 = **1,576 bytes per module**, and the budget was
8 MiB. The storage was five sixths empty and still evicting.

Profiled with the CDP sampling profiler (`tools/web_profile.py`), the busy
worker spent **42.7%** of its samples in `new WebAssembly.Module` / `new
WebAssembly.Instance` and the host glue around them, and another **9.8%** in
`jc_block_invalidate_range` + `x86p_jit_storage_invalidate` — the cache work
those evictions drive. Host draw was 0.31 ms/frame and upload 0.12 ms/frame
against a 727 ms frame, so this was never the renderer.

## Why the cap was never questioned

It was believed to bound renderer memory. Nobody had measured it. A probe built
to the actual shape of a translated block — 1,569 bytes, an imported shared
memory and twelve function imports — measured in Chrome 128:

    8192 module(s) of 1569 bytes each in 112.3 ms (13.7 us each), 26 MB RSS

**3.2 KB and 13.7 us per live module.** The whole 8,192 is 26 MB. The cap was
costing multiple seconds of translation per second to save 20 MB.

This also retracts a measured exclusion in #149 ("one wasm module per
translated block is not the cost"), which used empty and unimported modules and
a route with a twentieth of the translation rate.

## Fix

x86port `059244c`: `x86p_jit_storage_create()` takes `max_blocks`, the engine
passes the same number it sized its cache with, and the arena allocates that
many slots. The native storage names the parameter unused — machine-code blocks
share one region and are bounded by the bytes. Two costs the raised cap would
otherwise have exposed went with it: the arena finds a free slot through a free
list instead of scanning from zero (O(capacity) per translated block), and the
lowering scratch buffer is one worst-case module rather than the whole byte
budget.

Consumer: `src/native/x86_engine_jit_pool.c` raised `kCodeBytes` from 8 MiB to
32 MiB, because 8,192 blocks at the measured ~1.4 KB mean need ~12 MB and the
bytes must not quietly become the binding limit in the cap's place.

## After

Same route, same page command line, x2native rebuilt at the new pin:

    [HB] JIT: 220747782 blocks entered, 75636 translated ...; 11194466 bytes code
    [HB] JIT: 242355123 blocks entered, 75636 translated ...; 11194466 bytes code

Five seconds apart: **zero translations**. The working set now fits, 11.2 MB of
live module is held where the old cap pinned it to 1.6 MB, and guest throughput
went from 124,447 to **4,308,000 block entries per second**.

## What this did NOT fix

Frames. The same run sat at 10 presents while executing 4.3M blocks/s, with the
heartbeat attributing the interval to `KERNEL32.dll!WaitForSingleObject`
(4,810 ms in 600 calls) and a `SuspendThread`/`PulseEvent`/
`LeaveCriticalSection` trio at exactly 300 calls each. That is #149's cause,
not this one: the guest is not translating and not executing slowly, it is
waiting. This issue removed the largest CPU cost in the browser; it did not
make the browser product playable, and #149 should not be read as resolved.
