---
id: C143
kind: claim
status: holds
created: 2026-08-07
tags: pc,native,d3d8,graphics,text
---

## Claim

The native renderer draws the game's UI correctly, text included. The garbled/missing captions were one bug: fill_request carried the BOUND index buffer into non-indexed DrawPrimitive calls, so the backend drew a 202-primitive strip's 204 VERTICES as 204 INDICES out of whatever index buffer was bound (76).

## Evidence

scratch/screenshots/native.png reads 'SAVE FAILED!' and '[Esc] CANCEL  [Enter] RETRY' cleanly, captured headless. Before the fix, gpu_draw refused exactly one draw per frame with '204 index/indices ... needs 408 byte(s); the buffer is 152'; after it, 'refused 0' across 50k+ draws. Two hypotheses were tested and REJECTED with evidence first: state blocks (ApplyStateBlock reported the index binding unchanged every time) and a recycled GPU handle (which was wrong, but exposed a real missing device reference on bound resources -- fixed and kept).

## What would falsify it

a heartbeat line with a non-zero gpu refusal delta, or a capture in which the caption is absent or unreadable
