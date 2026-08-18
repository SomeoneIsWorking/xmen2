---
id: C213
kind: claim
status: holds
created: 2026-08-18
tags: crash,stack,gs,recompiler,abi
---

## Claim

The /GS "stack cookie" crash (issue #81) is NOT a buffer overrun: the cookie is
never written by anything but its owner, and the epilogue reads a slot four
bytes below the one the prologue wrote

## Evidence

`FUN_0046b750` is entered with esp `0x700ffee4` (printed by the override on
entry) and stores its cookie at entry_esp-4 = `0x700ffee0` -- `MOV [ESP+0x20],
EAX` after `SUB ESP,0x1c` + `PUSH ESI` + `PUSH EDI`. Its epilogue does `MOV
ECX,[ESP+0x20]` at `0x0046bad2` with esp `0x700ffebc`, reading `0x700ffedc`.
Those are different addresses, so the compare was never against the stored
cookie.

A write watch armed on `0x700ffedc` for a whole run recorded 2,000+ writes and
ZERO stores of the process cookie -- because that address is not where the
cookie goes. Armed on `0x700ffee0` (via `X2_SECURITY_WATCH=1`, which computes
it from the live entry esp) the very first write is the cookie store. The two
addresses differing IS the defect: the body returns to its epilogue with esp
one dword low.

`tools/stackcheck.py` then bounded where the drift is not: over the crashing
run, 1,323,140 of 1,400,175 dispatched calls checked against each callee's own
`RET` immediate, zero out of balance, with the 19 native overrides included.
The checker's selftest shows it flagging a 4-byte drift and excluding a
tail-calling body, so the clean result is a measurement rather than a check
that never fires.

## What would falsify it

A run where the cookie store and the epilogue read land on the SAME address and
the mismatch still fires (then something really does write the slot), or a
dispatched call found out of balance in the same window (then the drift is at a
boundary after all)
