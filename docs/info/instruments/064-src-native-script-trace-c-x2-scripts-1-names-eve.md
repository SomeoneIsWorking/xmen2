---
id: I064
kind: instrument
status: trusted
created: 2026-08-19
---

## Instrument

src/native/script_trace.c + X2_SCRIPTS=1 -- names every BehavEd script the run launches, records each launch caller, and traces startConversation, lockControls, the conversation-manager start (with flags, current line and the seen-line bitmap), the bitmap reset, and the tutorial spawner callback

## Validated by

Validated against cases that MUST be positive before any negative was believed: lockControls is called once at tutorial1.py's level entry and the trace shows exactly that; the conversation-start trace shows 0020 going flags 0x10->0x13 with a line selected while 0020b goes 0x18->0x10 with none; and the cutscene-player run records `nightcrawler_spawn` from the scheduler sentinel plus `nightcrawler_walk` from caller `0048a779` after the spawner callback. An earlier version of this instrument reported 0 calls for both commands because the handler addresses were read one dword out; the lockControls control is what exposed that, and it is kept for that reason.

## Known failure modes

(none recorded yet)
