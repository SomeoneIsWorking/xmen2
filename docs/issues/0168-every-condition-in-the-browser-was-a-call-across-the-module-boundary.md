---
id: 168
title: every condition in the browser was a call across the module boundary
status: resolved
symptom: every Jcc and SETcc called x86p_cond through the import table; now 99.6% are inline
state_items: S021
tags: web,browser,wasm,jit,codegen,performance
created: 2026-09-19
updated: 2026-09-24
---

# 0168 — every condition in the browser was a call across the module boundary

- **State items:** S021
- **Status:** landed and measured; the Add/Inc/Dec kinds are still on the helper
- **Found by:** the guest-worker census in #162, where `x86p_cond` sat at 0.91%
  with no work in it at all

## What it was

Every `Jcc` and `SETcc` a translated block executed called `x86p_cond` through
the runtime import table. A translated block is its own WebAssembly module, so
that is a cross-module call — the expensive kind on this target — and what it
bought was a switch over sixteen condition codes reading a flag tuple the block
had just written itself.

The information needed to answer the condition at translation time was already
in the lowering: `X86pWasmLower` records the flag KIND the previous instruction
left behind, because the carry-in derivation needed it. After a `SUB` or a
`CMP` the signed conditions are the signed comparison of the operands; after
`AND`, `OR`, `XOR` or `TEST` CF and OF are zero and four of the sixteen are
compile-time constants. None of that needs a call.

## What landed

x86port `f94ad4a` derives all sixteen conditions inline for the Sub and Logic
kinds, in a module of its own (`jit_wasm_cond.c`) so the remaining kinds can be
added one at a time. Two things it has to get right, and both have a falsifier
that fired while it was being written:

- **The signed conditions are signed.** Reading JL as an unsigned comparison
  passes every small-positive case and fails across the sign boundary. Making
  that mistake deliberately produced 19 failures in the suite.
- **The width is not bit 31.** An 8-bit `0xFF` has to compare as −1, and the
  kind does not say which width wrote it, so `last_w` travels with `last_kind`
  and the operands are normalised before the comparison. Removing the
  normalisation produced 132 failures, every one of them 8- or 16-bit signed.

`tests/test_wasm_cond.c` is 2,304 differential cases — six flag-writing forms
by sixteen conditions by eighteen operand pairs as `SETcc`, plus six pairs as
`Jcc` — against `x86p_cond` itself, and it asserts which path each shape took in
both directions: a `CMP` then `SETcc` must be inline, a lone `SETcc` and a
`SHL` then `SETcc` must not. A suite that only proved the new path could certify
a lowering that had silently stopped inlining.

## What it covers, on this title's code

x86port `2e1fa9c` splits the conditions that were not inlined into the two
causes that call for opposite fixes — a predecessor that never reached the
condition site, and one that reached it with a kind no derivation covers — and
`tools/jit_corpus.py` builds the corpus that measures it from the player's own
executable and the recovered function inventory.

Over all 16,451 recovered functions, lowered through the WebAssembly backend:

| | conditions | share |
|---|---|---|
| lowered inline | 53,601 | 96.9% |
| predecessor never recorded | 390 | 0.7% |
| predecessor of a kind with no derivation | 1,314 | 2.4% |

The running product agrees, which is the point of measuring both: over a Dead
Zone route the heartbeat reports **34,568 of 35,920 conditions lowered inline
(96.2%)**, 151 with an unrecorded predecessor and 1,201 of an underivable kind.

## What it is worth

`x86p_cond` was 0.91% of the guest worker, so that is the ceiling and 96.2% of
it is what this takes. The route's plateau read **11.840 +/- 0.040 presents per
second** afterwards against the **11.552 +/- 0.011** recorded for the previous
build — but the host was at load average 4.27 rather than 5.0, and a difference
this size is inside what that changes. **The frame figure is consistent with the
change and is not evidence for it**; the exact census is.

A confirming profile was not taken. The guest worker does not service the
DevTools protocol while it is running the guest, so an attempt to sample it took
Chrome down twice. That is worth its own fix and does not change this result:
the share `x86p_cond` can still hold is bounded by the 3.8% of conditions that
still call it, and the counter is exact where a sampled profile would not be.

## The measurement that lied, and why

The first reading after `f94ad4a` said **0 of 35,996 conditions lowered inline**
and cost most of a session. Everything said the build was live: the object was
in the tree, the symbol was in the module's map, `build/web` and
`build/release/web` had the same md5, and the wasm in the service worker's cache
and the one the server returned both hashed to the sha256 of the file on disk.

All of that was true and none of it was the question. The page had been loaded
while the service worker was still updating its cache, so the module the run
INSTANTIATED was the previous build; every check afterwards read the cache the
service worker had since refreshed. The checks were circular and could not have
come out any other way.

What broke it was a build whose byte count differed — 9,812,259 against
9,811,969 — and a heartbeat line whose TEXT had changed, so the running module
could be identified by what it said rather than by what a cache claimed to hold.
**A hash comparison between two things the page controls is not a check that the
page is running the build on disk.** Unregister the worker, delete its caches,
and confirm the running module by an observable that changed with the build.

## What is left

The 1,201 conditions of an underivable kind are Add, Inc, Dec and the explicit
EFLAGS the ADC/SBB path records. Add is the same shape as Sub with one sign
flipped; Inc and Dec differ from them only in preserving CF, which the
conditions that read CF must then still ask for. The module is shaped to take
one kind at a time, and the census is what ranks whether it is worth it: 2.4% of
conditions, against the 96.9% already taken.

## Closed (2026-09-24)

x86port cc33050 derives ADD, INC and DEC inline:
- After ADD, the unsigned conditions read the carry (`r < a`) and the signed
  ones the sign of the untruncated sum.
- INC and DEC read the carry they preserved, and compare the operand with a
  constant.

The lowering also had not been recording the operand width for INC and DEC.
The differential's new 8- and 16-bit cases, behind both STC and CLC, found
that at once.

Browser census on the Dead Zone route, same corpus as before (serving
10,074,450 bytes, the build on disk): **35,839 of 35,996 conditions lowered
inline (99.6%)**. The remaining 157 are:
- 138 conditions that open a block, where the predecessor is unknown at
  translation time;
- 19 after the explicit EFLAGS that ADC, SBB and POPFD record, which is a word
  and not a derivation.

Neither kind has an inline form to add. The route kept presenting about 260
frames per 5 s.
