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

## The falsifier above is the wrong question, and here is the right one

Written down, it does not survive reading. "A helper called from a block is
counted against the helper" is true and is not a problem: **self time IS time.**
The 11.76% now sitting in `x86p_jit_engine_run` is time spent in the dispatch
loop's own instructions, whoever called it and whatever it called. No call-tree
attribution can make that number smaller or larger.

What a call tree would answer is a different question, and not the one that
decides this: which *blocks* are hot. Issue #165 needs that. This one does not.

**What decides this is what fraction of block exits have a statically known
successor**, because that is exactly the fraction chaining can remove the hash
lookup and the second indirect call from. A block ending in a conditional
branch has two candidates; one ending in a computed jump or a return has none
that the translator knows.

That is a question for the engine, not the profiler: count exits by whether the
translator recorded a successor, and count how often the successor actually
taken was the one predicted. The counter must be able to report a low number --
a run that reports "95% predictable" without being able to report anything else
is not evidence -- so it wants a polymorphic-successor case to show the other
answer.

`X86pJitBlock` currently publishes `ends_in_branch` and no successor address,
so the translator would have to start recording one. That is the first piece of
this work and it is measurement, not chaining.

## The static half of that answer, measured

The dynamic half still wants the engine. The static half did not: x86port
`b1b2ea8` classifies every block's terminator in `tools/jit_coverage`, and
`tools/jit_corpus.py` builds the corpus from the player's own executable.

Over all 16,451 recovered functions, lowered by the WebAssembly backend:

| where the block goes | blocks | share |
|---|---|---|
| relative branch — successor known | 61,063 | 53.9% |
| ran out of room — next address known | 1,490 | 1.3% |
| **static successor** | **62,553** | **55.2%** |
| RET | 18,589 | 16.4% |
| indirect JMP/CALL | 31,120 | 27.5% |
| far or other | 1,010 | 0.9% |

**Each block counts once**, so a hot loop and a function that never runs weigh
the same. That is the number's limit and it cuts in the optimistic direction
for chaining: hot code is loops, loops end in relative conditional branches, and
the 27.5% indirect is inflated by the import thunks and virtual dispatch that a
static walk counts once each and the run enters rarely. So 55.2% is a floor for
what chaining could reach, not an estimate of it.

The same tool over the same bytes with the x64 backend reports 70.2% static,
because that backend's blocks are shorter and there are more of them — which is
the instrument responding to a real difference rather than printing a constant,
and is why it can be believed here.

Nothing is decided by this alone. What it rules out is the cheap objection:
chaining is not chasing a handful of exits.

## The numbers, refreshed after #162's pop fusion

Guest worker, Dead Zone route, 25s, 96,169 samples:

| frame | share |
|---|---|
| `x86p_jit_engine_run` | 11.76% |
| `x86_engine_jit_intercept` | 1.35% |
| **dispatch, total** | **13.11%** |

Up from 12.4%, which is what happens to a share when something else in the
denominator gets cheaper. It is now the largest single item after
`x86p_x87_arith_raw`.
