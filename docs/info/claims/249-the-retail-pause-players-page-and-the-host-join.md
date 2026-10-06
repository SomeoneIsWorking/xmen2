---
id: C249
kind: claim
status: holds
created: 2026-08-22
tags: input,co-op,retail,pause,participation
depends: src/native/player_participation.c#x2_player_participation_apply, src/native/player_participation_probe.c#x2_player_participation_probe_report
---

## Claim

The retail pause Players page and the host join bridge use the same participation owner

## Evidence

XMen2.exe FUN_0048de40 returns singleton 0x0072c4f0/vtable 0x006898c4; pause handler FUN_005cdb50 uses +0x10 query, +0x14 join, +0x18 leave, +0x68 reconcile, and src/native/player_participation.c invokes those exact slots without active-flag/count writes. In a bounded `X2_VIRTUAL_PAD=1` live run, P2's session assignment left the reported retail mask at `0x1`, its assigned Start changed it to `0x3`, and clearing ownership returned it to `0x1`. Opening the retail pause Players page at `0x3` showed P1/P2 green and P3/P4 red. See docs/RE/co_op_participation.md.

A separate live falsifier opened retail Players while P2 had None, selected P2 and pressed Enter. The next safe pump logged `PLAYER-PARTICIPATION: retail reconcile; join=0x00 leave=0x02`, and `/input` queried mask `0x1` with P2 inactive. Thus an unchanged None assignment is re-enforced against an external retail join rather than relying only on assignment-change transitions.

## What would falsify it

A newly opened live pause Players page reports a different active set than tools/x2ctl.py input after a host join/leave transition, or a rebuilt executable changes the four evidenced vtable targets/ABIs.
