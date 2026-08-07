---
id: I041
kind: instrument
status: trusted
created: 2026-08-07
---

## Instrument

x2native liveness heartbeat (X2_HEARTBEAT, src/native/heartbeat.c)

## Validated by

Run against BOTH classes on the same build. Positive: while the game rendered, it printed rising crossings with presents +300 per 5s (60fps). Negative: after the frame limiter hung it printed 'NO frame was presented in that time' and 'NOTHING was drawn' with the crossings STILL rising, which is the distinction the instrument exists for and the one nothing else could make. It runs on its own thread, so it also reports when the guest is blocked in host code ('the guest executed NOTHING'). Blind spot: it reads counters without a lock, so a single line's delta can be torn; and the D3D8 counters only exist once a device does, which it states.

## Known failure modes

(none recorded yet)
