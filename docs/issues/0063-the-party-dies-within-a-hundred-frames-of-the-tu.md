---
id: 63
title: The party dies within a hundred frames of the tutorial loading
status: resolved
symptom: The tutorial reaches its first conversation, then schedules the default all-X-Men-eliminated route even though no combat occurred.
tags: pc,native,gameplay,tutorial,party,x87
created: 2026-08-13
updated: 2026-09-04
---

## Retained retail behavior

The default game-over notification is delivered by `FUN_0041e380`. The
payload carries reason 0 (`GAMEOVER_DEFAULT`). `FUN_0041de40` schedules that
notification three seconds ahead, and its caller at 0x0042a137 is inside
`FUN_00429de0`, the party-wipe decision.

`FUN_00429de0` enumerates the roster at 0x0042a064. A count less than or equal
to zero reaches the game-over path at 0x0042a0e7. Otherwise each handle is
resolved by `FUN_004654b0`; unresolved handles are skipped, and any actor whose
health at `actor+0x27c` is greater than the 0.0 constant at 0x00680030 cancels
the wipe.

The roster producer is `FUN_0046d460`, reached through vtable slot
`0x00686e1c + 0x120`. It iterates the collection from `FUN_004ab770`, publishes
selected handles through the result bank at 0x007298e0, and stores the count at
0x007298f4. The tutorial capture contained four actors, but the selector
`FUN_0046a880` rejected them because their health values were -78, -78, -90,
and -66. Actor flag 0x00100000 is the active/enabled bit; vtable slot +0xe4
sets it and +0xe8 clears it.

`FUN_00422b40` stores maximum health at `actor+0x284`, then initializes current
health from that maximum minus an integer stat delta. The four matching maximum
health values were positive 78, 78, 90, and 66. The delta is computed at
`FUN_004b7780:0x004b786f` by:

```text
FSUBR ST0,ST1
```

The architectural result is `ST0 = ST1 - ST0`. Reversing those operands makes
the delta positive and twice the hero's health, which produces the observed
negative values. The roster and game-over code then behave consistently with
their retail contracts.

## Current conformance requirement

The shipping x86port JIT must implement the two-register `FSUBR` direction
above and cover it through its independent test-only CPU oracle. A
representative tutorial run must show positive initial health and must not
schedule `GAMEOVER_DEFAULT` before damage or an intentionally empty roster.

## What would reopen it

A runtime-JIT tutorial run that supplies positive maximum health but produces
negative current health at `actor+0x27c`, or schedules reason 0 with a living,
resolvable roster, reopens the issue. That result must be diagnosed at the
first divergent register or memory write rather than bypassing the game-over
route or fabricating a party member.
