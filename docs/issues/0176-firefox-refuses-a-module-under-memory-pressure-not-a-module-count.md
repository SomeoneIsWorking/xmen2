---
id: 176
title: Firefox refuses a WebAssembly module under memory pressure, not at a module count
status: resolved
symptom: the browser run aborted about four seconds into guest execution with "The game stopped: native code called abort()"; Chrome ran the same build
state_items: S021
tags: web,browser,firefox,zen,jit,x86port,wasm
created: 2026-09-19
updated: 2026-09-19
---

# 0176 — Firefox refuses a WebAssembly module under memory pressure, not at a module count

State item: S021 (web product). RESOLVED: the game renders and presents in
Zen 1.22.2b (Firefox 156). What remains is frame rate, which is issue #162,
#166 and #169 territory, not this one.

## What a player saw

The game started and stopped after about four seconds with "The game stopped:
native code called abort()". Chrome ran the same release.

## The first measurement, and the wrong lesson taken from it

On release `d4ab197d` the runtime published **one module per translated block**
and Firefox refused the 16,112th with ~49,000 of the arena's 65,536 slots free:

```
WebAssembly publication: the engine rejected a 2735-byte module:
InternalError: out of memory (16110 live of 65536 slot(s); 16112 published)
```

A later run released 1,008 modules and was refused again with `published`
unchanged at 16,112, which was read as "the port never gets back what it
releases". `scratch/zen/module_ceiling.py` asks the browser the same question
directly and gets the opposite answer: a PAGE that fills to a refusal, releases
1,021 and tries again is **accepted at once, with no yield**. Both readings are
real. The reconciliation is below, and it is not a leak.

## The cause: one module per block, and a refusal that is about memory

Firefox's refusal is `InternalError: out of memory`, and it is what it says.
It does not arrive at a fixed module count: with compaction landed, the same
browser refused a **121,950-byte** module with **863** modules live. A design
that spends one engine module per translated block walks into that wall in
seconds whatever the wall's exact shape is.

## What was fixed

1. **Compaction.** A block is published alone so it can run immediately, then
   32 are re-lowered into one shared module and each block's indirect-table
   entry is ADOPTED onto the new module -- its address never changes, so every
   cached reference and every compiled call keeps working -- and the singles
   are released. `X86P_WASM_COMPACT_BATCH`.
2. **Eviction in module units.** Dropping one block frees nothing when 32 share
   a module, so the engine could not make room and flushed the whole cache
   instead: 94 flushes, a 14.4 MB working set down to 50 KB, and
   `RuntimeError: indirect call to null`. `x86p_jit_storage_evict` now empties
   one module and reports the guest ranges it dropped.
3. **A ceiling is a hypothesis.** A refusal recorded the live count as a
   permanent ceiling, so ONE memory-pressure refusal made the arena evict live
   code for the rest of the run: 1,641 evictions and 51,664 retranslated blocks
   in five seconds. x86port `9201965` retires the ceiling once the arena has
   backed off below it by its headroom, and counts `ceilings_learned` against
   `ceilings_retired` so a run says which kind of host it is on.
4. **Only an ENGINE refusal teaches a ceiling.** A module the engine will not
   compile says nothing about how many it will hold; conflating the two had
   capped one run at 2,363 modules. `jit_wasm_host.c` separates
   `CompileError`/`LinkError`/`TypeError` from everything else.

A fifth defect was in the way and is its own result: Emscripten's Dawn binding
passed the entire wasm heap to `setBindGroup`, which Firefox rejects once the
heap exceeds its 2 GB ArrayBufferView limit, killing the render thread. Fixed
in the maintained fork `SomeoneIsWorking/emdawnwebgpu` (`500f12c`) and wired
through SDL and `shared/web-port`.

## The measurement now

Same route, same browser:

```
JIT modules: 61574 translated block(s) were gathered into 1922 shared module(s)
the engine evicted: 135 time(s) dropping 3335 block(s) of 61574 translated
  asked for by: 0 the byte budget, 0 the module slots, 135 the live-module ceiling
  the engine's refusals put 2 ceiling(s) in force, 2 of which did not survive the back-off
0 cache flushes, 45494818 bytes code
```

Against 1,641 evictions and 51,664 dropped blocks before the ceiling was made
retirable. The run renders, presents, and does not abort.

## Instrument defects found while measuring this

- A console listener registered in a Marionette chrome sandbox reported an
  empty buffer through a run that printed 1,300 lines. The browser's own stdout
  (`devtools.console.stdout.content`) has everything, including the worker's.
  `scratch/zen/drive.py` reads that and refuses when it sees zero lines.
- The engine's refusal was cut off mid-sentence at a 192-byte reason buffer,
  removing the denominators. Now 512 here and in `x86port`.
- The service worker did not answer navigations that carried a query string, so
  every run with page arguments lost cross-origin isolation and latched
  `Browser isolation is unavailable` in sessionStorage. Fixed in `web-port`
  with a regression case in `tests/sw_verify.mjs`.
- "0 engine(s) have stopped gathering for good" is what disproved a
  `cannot_compact` hypothesis before it was acted on.
