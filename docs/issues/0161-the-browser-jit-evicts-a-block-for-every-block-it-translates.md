# 0161 — the browser JIT evicts a block for almost every block it translates

- **State items:** S021
- **Status:** measured and attributed; the arena has not yet been resized
- **Follows:** #157, which removed the cost that was hiding this one

## The measurement

Dead Zone route, browser, with the invalidation counters added to x86port and
this port's heartbeat. Two consecutive five-second intervals:

| | at 23:44:18 | at 23:44:23 | delta |
|---|---|---|---|
| blocks translated | 22,298 | 38,068 | **+15,770** |
| blocks dropped | 14,106 | 29,876 | **+15,770** |
| engine invalidation calls | 13,486 | 27,570 | +14,084 |
| this port's notifications | 120 | 143 | +23 |
| cache flushes | 0 | 0 | 0 |

**Every block translated in that interval was thrown away in the same
interval.** The run sustains roughly 3,000 translations a second and retains
none of them.

## Who is doing it

Not this port. Its three guest-memory operations — map, protect, release — sent
23 notifications in the interval that produced 14,084 engine calls. Over the
whole run they reached 500 against 106,000. The remaining 99.5% is
`translate_at` in `shared/x86port`, calling `x86p_jit_storage_victim` and
dropping a block to make room for the one it is about to translate.

The instrument said so only after being fixed: the first version counted the
engine's own reclaim and the embedder's notifications as one number, and that
number read as this port notifying tens of thousands of times a second. x86port
`c2c7331` separates them, and `test_wasm_runtime` now requires the eviction
count to move while the embedder count stays at zero.

## Why the arena is full

`src/native/x86_engine_jit_pool.c` gives each browser worker
`kCodeBytes = 32 MB` and `kCacheBlocks = 8192`. The same heartbeat reports
**13.6 MB of code in use**, so the byte budget has more than twice the headroom
it needs and the 8,192-block cap is what binds. The game's live working set of
translated blocks is larger than 8,192, and the arena turns over completely
several times a second.

This is the same defect as the one the comment above those constants describes
— a fixed 1,024-module cap that made "7 of every 8 entries here never hold
anything" — one cap further up. Raising it then took retranslation from 7,244
per second to the 3,000 measured here; the cap was raised, not removed, and the
working set is still above it.

## What to do, and what not to

Not a bigger number chosen to make the symptom go away. The working set has
never been measured: the honest step is to run with a cap far above any
plausible requirement, read where `blocks_translated` plateaus once eviction
stops, and size the shipping cap from that with a stated margin. A cap that is
simply doubled will hide this until the next map.

Two constraints on the answer:

- Blocks and bytes must move together. The comment on `kCodeBytes` is right
  that bytes silently become the binding limit otherwise; at the measured mean
  of about 1.7 KB a block, 24,576 blocks need about 40 MB.
- Each `WebAssembly.Module` is a permanent engine object and the browser is
  already committing 2.5 GB for the guest window (#159). The measured working
  set is what says whether the two fit together, which is another reason to
  measure it rather than pick a number.

## What would falsify this

If a run with the cap raised far above the working set still evicts at the same
rate, the cap is not what binds and the victim loop is being entered for
another reason — the byte budget, or a storage that reports no room while
holding some.
