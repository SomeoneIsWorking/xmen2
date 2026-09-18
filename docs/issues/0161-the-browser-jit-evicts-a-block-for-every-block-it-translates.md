# 0161 — the browser JIT evicts a block for almost every block it translates

- **State items:** S021
- **Status:** measured and attributed; the arena has not yet been resized
- **Follows:** #157, which removed the cost that was hiding this one

## The measurement

Dead Zone route, browser, with the invalidation counters added to x86port and
this port's heartbeat.

**Read the phase before reading the numbers.** The interval below is the map
LOADING, where translation is 3,000-6,000 blocks a second. Once the map is
running the baseline settles to about 1,000 a second — still all of it wasted,
but a much smaller share of the frame. The first version of this issue quoted
the loading interval as though it were the steady state, which overstates what
fixing it is worth by about five times.

Two consecutive five-second intervals, during loading:

| | at 23:44:18 | at 23:44:23 | delta |
|---|---|---|---|
| blocks translated | 22,298 | 38,068 | **+15,770** |
| blocks dropped | 14,106 | 29,876 | **+15,770** |
| engine invalidation calls | 13,486 | 27,570 | +14,084 |
| this port's notifications | 120 | 143 | +23 |
| cache flushes | 0 | 0 | 0 |

**Every block translated in that interval was thrown away in the same
interval.** Nothing is retained, at any phase: over a 452-second baseline run
the arena translated 654,779 blocks and held 8,192.

Translation rate by age on that run — the shape that says which phase is which:

| age | blocks/s |
|---|---|
| 6 s | 3,846 |
| 51 s | 4,533 |
| 56 s | 6,316 |
| 101 s | 685 |
| 151 s | 2,782 |
| 251 s | 886 |
| 402 s | 1,425 |
| 452 s | 462 |

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

## The working set, measured

Same route, same build, with `jit.blocks=131072` and `jit.code_mb=512` — far
above any plausible requirement, so the run could translate until it stopped
needing to. Translation climbed and then stopped:

| age | blocks translated | code bytes |
|---|---|---|
| 16 s | 25,282 | 39.7 MB |
| 26 s | 42,694 | 68.1 MB |
| 41 s | 57,693 | 92.3 MB |
| 75 s | 61,141 | 98.1 MB |
| 105 s | 61,147 | 98.1 MB |
| 195 s | 61,211 | 98.2 MB |

Six blocks in thirty seconds, then three in the next ninety. **The Dead Zone
route's working set is about 61,200 blocks and 98 MB of module**, against a
shipping arena of 8,192 blocks and 32 MB. Evictions over the whole run: zero.
Mean block: 1.6 KB, so BOTH limits are wrong — the byte budget would have bound
at about 20,000 blocks even with the block cap removed.

## What it is worth, which is less than it looks

Presents, same route, both arenas, by age:

| | 8,192 blocks | 131,072 blocks |
|---|---|---|
| 200 | 80 s | ~72 s |
| 400 | 120 s | ~108 s |
| 800 | 190 s | ~172 s |
| ~910 | ~205 s | 195 s |

About **10% more frames**, not the doubling that "half the guest worker is
translation and invalidation" suggested. That figure came from a profile taken
during loading. At steady state translation is around 1,000 blocks a second,
which is a few percent of the worker, and 10% is what removing it returns.

Loading is where the rest of the win is: the first minute translates at
3,000-6,000 blocks a second and it is all thrown away.

**And it moved the bottleneck rather than removing it.** With the arena large
enough, a profile of the busy worker puts `x86port JIT translation` and
`wasm compile/instantiate` at 0.00% each, and x87 emulation at about 47% —
issue #162.

## The size to ship, and why that one

**65,536 blocks and 128 MB**, matching the desktop's block count. It covers the
measured 61,200 blocks and 98 MB with about 7% of block headroom and 30% of
byte headroom, and both limits move together so neither silently becomes the
binding one: at the measured mean of 1.6 KB a block, 65,536 blocks want 105 MB.

What that number is NOT is a doubling until the symptom goes. It is this
route's measured working set plus a margin, and the margin is small enough to
be honest about: **one route has been measured.** Another map will have its own
working set, and if it is larger this will evict again — the heartbeat will say
so by name, which is the whole reason the counters exist.

The memory cost is real and belongs beside #159: 98 MB of module per worker on
top of 2.5 GB of committed guest window. This run left the 15 GB host with
2 GB free. Shrinking the window is what makes room for the arena, so the two
issues are one decision.

## What to do, and what not to

Measure the second route before trusting the size above for every map. The
procedure is the one that produced it: set `jit.blocks` and `jit.code_mb` far
above any plausible requirement, run until `blocks_translated` stops moving and
the heartbeat reports zero evictions, and read the plateau.

And do not read this issue as the frame-rate fix. It is worth 10% and it moves
the cost to x87 (#162), which is now the larger one.

## What would falsify this

If a run with the cap raised far above the working set still evicts at the same
rate, the cap is not what binds and the victim loop is being entered for
another reason — the byte budget, or a storage that reports no room while
holding some.
