---
id: I009
kind: instrument
status: trusted
created: 2026-08-05
---

## Instrument

vendor/xboxrecomp/tools/validate_ordinals.py (kernel ordinal table cross-check)

## Validated by

Seen both ways in one session: FAIL on 45 real mismatches in the newly-covered bridge/arg-size tables, OK after the remap, with the thunk and doc checks passing throughout. It states its own limit: it compares NAMES only, never whether a bridge body implements the semantics its name claims.

## Known failure modes

(none recorded yet)
