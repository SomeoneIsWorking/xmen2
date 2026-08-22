---
id: I067
kind: instrument
status: trusted
created: 2026-08-22
---

## Instrument

`src/native/autosave_runtime.c`: production autosave transition and result
counters in the live `/save` report

## Mechanism validation

`test_autosave_policy` drives failed and successful retained map results,
main-menu cancellation, a manager-busy reset, the exact 64-idle-poll boundary,
one-shot completion and later rescheduling. `test_autosave_format` drives the
shipping payload-tag parser through the observed positive shape and malformed
tags. `test_autosave_storage` injects each pre-rename failure and proves the
prior file survives. `check_autosave_wiring.py` pins retained-map -> trace mark
-> schedule order, unconditional 0x00484ce0 registration, independent menu
cancellation, input-poll ownership and serializer -> header -> directory ->
transactional publish order. The Clang x2native build links that chain.

The report prints successful/total map returns, attempts/scheduled,
successes/attempts and failures/attempts even at zero, plus menu cancellation,
idle/deferred polls, manager mode, pending/active state, last result class and
`errno`. `X2_SAVE_TRACE=0` suppresses optional evidence events but does not
disable the production map wrapper, cancellation or autosave.

## Trust validation

Two consecutive native product runs supplied the OTHER answer on 2026-08-22.
The first began without `autosave.save`; at frame 428 the report read
`map-success=2/2 scheduled=2 cancelled-menu=1 idle-polls=64 attempts=1/2
success=1/1 fail=0/1 last=succeeded`. A 195,716-byte `autosave.save` appeared
while `saveslot0.save` retained both size and mtime. The next Boot Continue
named `load_0055fcd0=autosave.save`, crossed 0x0049f140, manager 0x004aed10
mode 3/state 1 and deserializer 0x0046e2b0, transitioned mode 3 -> 0, and
rendered Sanctuary. The report therefore distinguishes cancellation from a
real attempt and its success counter corresponds to a retail-loadable file.

## Known failure modes

The counters alone cannot prove the contents reached disk or that retail accepts
the file; the trust validation required separate host file inspection and an
exact-leaf load for those observations.
A process ending before 64 consecutive manager-idle input polls can legitimately
report a queued request with zero attempts.
