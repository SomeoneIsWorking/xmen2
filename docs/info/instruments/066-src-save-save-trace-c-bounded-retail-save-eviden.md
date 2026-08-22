---
id: I066
kind: instrument
status: pending
created: 2026-08-22
---

## Instrument

src/save/save_trace.c bounded retail save evidence collector + X2_SAVE_TRACE live /save report

## Validated by

Mechanism only: `test_save_trace` drives the exact production collector through
disabled refusal, 0/0 report, yes/no/unknown branch answers, manager metadata
fields, mode cycles, 64-event overwrite, invalid inputs, label truncation and
exact-capacity refusal (131 assertions). The Clang x2native build proves all
retained-body override seams and the `/save` route link; the routing audit pins
15/15 exact registrations, 364 generated direct edges through `DISPATCH` and
15/15 retained original bodies. End-to-end retail hook firing remains pending
one targeted live run, so the combined live instrument is not trusted yet.

## Known failure modes

The collector/report core is trusted, but the retail address-to-event wiring is
only mechanism-verified until a targeted run produces the expected positive
events. The retained-body overrides are registered by default before startup;
`X2_SAVE_TRACE=0` disables them and cannot be reversed after the process has
started. A zero with `enabled=0` is a refusal, not evidence that the retail path
was absent.
