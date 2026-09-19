---
id: 176
title: Firefox will not take more than ~16k WebAssembly modules, and releasing them gives nothing back
status: open
symptom: the browser run aborts about four seconds into guest execution with "The game stopped: native code called abort()"; Chrome runs the same build
state_items: S021
tags: web,browser,firefox,zen,jit,x86port,wasm
created: 2026-09-19
updated: 2026-09-19
---

# 0176 — Firefox will not take more than ~16k WebAssembly modules, and releasing them gives nothing back

State item: S021 (web product)
Status: OPEN. The cause is established; the fix is architectural and is not
written yet.

## What a player sees

In Zen 1.22.2b (Firefox 156) with WebGPU enabled, the setup page now opens
(issue 0175) and the game starts, then stops after about four seconds with
"The game stopped: native code called abort()". Chrome runs the same release.

## The measurement

From the browser's own stdout, on release `d4ab197d`:

```
WebAssembly publication: the engine rejected a 2735-byte module:
InternalError: out of memory
(16110 live of 65536 slot(s); 16112 published and 2 released so far)
```

The runtime publishes one WebAssembly module per translated block. Firefox
refuses the 16,112th with roughly 49,000 of the arena's 65,536 slots still
free, so the arena's own capacity was never the limit.

## What eviction is worth: nothing

The arena was changed to learn a ceiling from a refusal and, after a second
refusal below that ceiling, to keep one slot in sixteen free below it
(`shared/x86port` 1550815). On release `39d66027` that produced the batch
eviction it was meant to:

```
(15104 live of 65536 slot(s); 16112 published and 1008 released so far;
 the arena will work at no more than 14160 of a 15104 ceiling from here)
```

`published` is **16112 in both runs**. The arena released 1,008 modules and the
very next instantiation was refused without a single new publication in
between. Firefox is not counting the arena's live modules, and it returns
nothing for a released one inside the run — so no eviction policy, at any
headroom, can keep this design alive. The run still aborts.

## What this rules out

- Not the arena's capacity: ~49,000 slots were free at the wall.
- Not a leaked reference in the host glue: `host_release` deletes the map entry
  and clears every table entry, and the compiled `WebAssembly.Module` is a
  local that is never stored (`jit_wasm_host.c`).
- Not recoverable by evicting harder: measured above.
- Not the guest re-translating the same code: the engine's own invalidation
  counters report 21 calls over 20 MB dropping **0** blocks, and 0 evictions
  before the wall.

## The fix this needs

Fewer, larger modules: the per-block module has to become a module holding many
blocks, so the same guest coverage costs a fraction of the module count. That
changes the translate/publish/resolve/release boundary in `jit_wasm_arena`,
`jit_storage_wasm` and `jit_wasm_host`, and it has to keep a block executable
at the moment it is translated, which is what makes it more than a batching
loop. Sizing and design are not done.

A bounded interpreter fallback is not available to take the overflow: the
browser product has no interpreter (`product fallback unavailable`), and the
project's rules keep one diagnostic-only in any case.

## Instrument defects found while measuring this

- A console listener registered in a Marionette chrome sandbox reported an
  empty buffer through a run that printed 1,300 lines. The browser's own stdout
  (`devtools.console.stdout.content`) has everything, including the worker's.
  `scratch/zen/drive.py` reads that and refuses when it sees zero lines.
- The engine's refusal was cut off mid-sentence at a 192-byte reason buffer,
  removing the denominators. Now 512 here and in `x86port`.
- The heartbeat's JIT line reports the calling thread's pool, so it printed
  "0 blocks entered, 0 translated ... the code arena holds the whole working
  set reached so far" during the run that had just published 16,112 modules.
  That line names the wrong owner and is not yet fixed.
