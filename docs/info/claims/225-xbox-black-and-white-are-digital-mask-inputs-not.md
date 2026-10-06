---
id: C225
kind: claim
status: holds
created: 2026-08-20
tags: input,xbox,controller
depends: tools/xbe_query.py#cmd_func
---

## Claim

Xbox Black and White are digital-mask inputs, not entries in the controller's 30-float physical array

## Evidence

Xbox sub_00163E40 clears all 30 floats, writes only the four stick axes, and passes the digital mask separately to sub_0015FD90. sub_00163240's Black/White records carry platform codes 9/8 and bytes 8/9, but those bytes do not index floats written by the poller.

## What would falsify it

a trace of the retail Xbox poller showing Black or White write float slot 8 or 9 while leaving the digital mask unchanged
