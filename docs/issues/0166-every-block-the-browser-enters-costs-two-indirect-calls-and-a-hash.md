# 0166 — every block the browser enters costs two indirect calls and a hash

- **State items:** S021
- **Status:** measured, not started
- **Found by:** categorising the guest worker's samples by owner after the #162
  widening work, which put dispatch second behind x87

## The number

Of the browser's guest worker, over a 25-second window of the Dead Zone route:

| category | share |
|---|---|
| x87 emulation | 39.1% |
| all translated guest code | 21.9% |
| **dispatch (`x86p_jit_engine_run` + `x86_engine_jit_intercept`)** | **12.4%** |
| flags and ALU helpers | 4.5% |
| SSE/SIMD helpers | 4.2% |

Dispatch costs more than half of what all the guest's own translated code costs.

Per second this route enters about 3.7M blocks (18.4M per five-second heartbeat
window). 12.4% of a roughly saturated core works out near 130 cycles of dispatch
per block entry, which is several times what the work in the loop should be.

## Where it goes, from reading the loop

`jit_engine.c:403` runs, per block entry:

1. `e->intercept(cpu, ...)` — an **indirect call out to the consumer**, on every
   block, before anything else.
2. `jc_block_lookup(e->cache, cpu->eip)` — a hash lookup keyed on guest EIP.
3. `fn(cpu)` — the block itself, a **second indirect call**; in WebAssembly a
   `call_indirect` carries a table bounds check and a runtime type check.

Then a counter, and on the common exit (`kX86pJitExitBlockEnd`) a `continue`.
The exit handling is a chain of compares and is not the cost.

**The intercept body is not the problem and should not be "optimised".**
`src/native/x86_engine_intercept.c` is already a range check, a frame-return
compare and a bloom probe, with the expected branch marked on each — about six
instructions on the path it almost always takes. Its 1.53% is the crossing, not
the work. Rewriting it would move nothing.

## Why this is structural rather than a tuning problem

Two of the three costs are there because the dispatcher asks a question per
block that is nearly always answered the same way.

- The **intercept crossing** could largely be decided when the block is
  translated: `x86_engine_jit_boundary` already exists and answers for an
  address. What stops it being purely static is the frame-return check, which
  compares EIP against a value the current run owns. That is one compare, and
  it does not need a call into the consumer to make it -- but moving it means
  x86port taking a value rather than a callback, which is an API change.
- The **hash lookup and the second indirect call** are what block chaining
  removes. A block ending in a direct branch to a known target returns to the
  dispatcher only to be sent straight back to a block the translator already
  knew. WebAssembly cannot be patched in place, so a chain here means the block
  calling its successor's table slot itself, and that has to keep the step
  budget, the intercept and invalidation correct -- a chain must break when its
  successor is invalidated.

## What this is worth, and what it is not

Nothing here has been built or prototyped, so 12.4% is the ceiling and not a
promise. Removing the intercept crossing alone is worth something near 2%.

It is worth saying plainly that 2% is **below what presents/s can resolve on
this route** -- one frame per five-second window is 1.75% -- so changes of this
size are ranked and verified on the profile, and only their accumulation shows
up in the frame rate. Issue #162 has the demonstration.

## What would falsify the ranking

An attribution of time to callers rather than to self. The 21.9% for translated
guest code and the 12.4% here are self time, so a helper called from a block is
counted against the helper. If a call-tree attribution shows dispatch is largely
inside a few hot blocks that chaining would not reach, the estimate is wrong.
The same attribution is what issue #165 needs to be re-ranked, so it is one
piece of work answering two questions.
