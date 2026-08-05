---
id: I013
kind: instrument
status: trusted
created: 2026-08-05
---

## Instrument

tools/xbox_discover.sh -- runtime discovery loop driver (run -> seed -> re-lift -> rebuild)

## Validated by

Both classes exercised on 2026-08-05. POSITIVE: rounds 1-3 each named a real unresolved indirect-call target (0x002AC090, 0x00270AF0, 0x002AD0A0), seeded it, and the next round got further (26976 -> 48500 -> 55406 indirect calls executed), so the loop can report progress. NEGATIVE/refusal: with scratch/.xbox_discover.lock held it refuses and names the holding pid instead of running -- verified against a held lock, exit 1. THE LIE IT TOLD BEFORE THE LOCK (issue #3): with a second loop running from an earlier session, both drivers truncated and appended to the same scratch/logs/xbox_discover.log, so ONE log showed rounds 1,2,3 and 5 interleaved as if from one run, and the resulting 'a seed did not land' / 'undefined reference' failures read as translator bugs when they were collisions. Read the round numbers for monotonicity before trusting a discovery log.

## Known failure modes

(none recorded yet)
