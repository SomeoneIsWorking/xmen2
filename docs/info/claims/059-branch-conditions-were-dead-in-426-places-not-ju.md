---
id: C059
kind: claim
status: holds
created: 2026-08-05
tags: xbox
depends: xbox/xboxrecomp.lock
---

## Claim

Branch conditions were dead in 426 places, not just re-evaluated: a jcc in a block entered by a JUMP had no tracked flag setter, so the lifter emitted 'if (_flags ...)' -- a variable declared 0 and never assigned. Flag state propagated only along the fall-through chain.

## Evidence

426 'if (_flags' in the generated C for 25,778 functions, and _flags is assigned nowhere. Propagating flag state over the CFG (two passes: learn which blocks set flags, then a fixpoint where a block inherits only when every predecessor agrees) plus keeping the fall-through edge takes sub_0026E740 from 1 to 0. Discriminated on real data: the regression test fails on the old code and passes on the new. Safe across blocks only because a setter's operands are snapshotted into _flg0/_flg1 (C050).

## What would falsify it

the CFG merge gives up when predecessors disagree, so some always-false branches remain; the count is the measure, and dropping the fall-through edge entirely took it from 395 to 722, which is how the two-source rule was arrived at
