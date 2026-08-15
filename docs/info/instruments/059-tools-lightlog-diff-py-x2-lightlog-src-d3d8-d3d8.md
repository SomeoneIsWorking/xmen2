---
id: I059
kind: instrument
status: trusted
created: 2026-08-15
---

## Instrument

tools/lightlog_diff.py + X2_LIGHTLOG (src/d3d8/d3d8_device.c) -- the port's light path in the control's exact format, compared

## Validated by

--selftest runs a pair that MUST differ (a black diffuse that is also never enabled -> exactly 2 differences), a log compared against ITSELF (0 differences), and a log with lines but no SETLIGHT, which it must REFUSE -- checked by forking, so a refusal that stopped refusing would fail the test rather than pass it quietly. On real data it found the port and control byte-identical at the menu (13 indices, same values, same enable pattern), which is the negative it had to be able to give.

## Known failure modes

(none recorded yet)
