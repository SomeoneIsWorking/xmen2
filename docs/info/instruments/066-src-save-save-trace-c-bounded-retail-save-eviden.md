---
id: I066
kind: instrument
status: trusted
created: 2026-08-22
---

## Instrument

src/save/save_trace.c bounded retail save evidence collector + X2_SAVE_TRACE live /save report

## Validated by

`test_save_trace` drives the exact production collector through
disabled refusal, 0/0 report, yes/no/unknown branch answers, manager metadata
fields, mode cycles, 64-event overwrite, invalid inputs, label truncation and
exact-capacity refusal (131 assertions). The Clang x2native build proves all
retained-body override seams and the `/save` route link; the routing audit pins
15/15 exact registrations, 364 generated direct edges through `DISPATCH` and
15/15 retained original bodies. A 2026-08-22 default native run then produced
positive end-to-end events for all of main-menu Build/Show and `main.engb`, a
mode-3 `saveslot0.save` load through 0x0055fcd0 -> 0x004aed10 -> 0x0046e2b0,
the 3 -> 0 mode cycle, and a mode-4 extraction save through state 0x1d and
both writer return sites. The host file remained 195,716 bytes and its mtime
advanced to 2026-08-22 14:19:30.093929267 +0300. That positive/OTHER answer
validates the combined runtime wiring, rather than only its collector core.

## Known failure modes

The retained-body overrides are registered by default before startup;
`X2_SAVE_TRACE=0` disables them and cannot be reversed after the process has
started. A zero with `enabled=0` is a refusal, not evidence that the retail path
was absent. Static RE corrected two initial labels after that run: 0x0049f860
is `lockCombat`, not a zone request, and 0x0049f140 is CompleteLoad while
0x0049f150 is DeleteCorrupt. The instrument now uses those exact meanings.
