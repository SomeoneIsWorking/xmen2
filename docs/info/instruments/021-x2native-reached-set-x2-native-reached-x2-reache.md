---
id: I021
kind: instrument
status: trusted
created: 2026-08-06
---

## Instrument

x2native reached-set (X2_NATIVE_REACHED / X2_REACHED)

## Validated by

Both classes are run on EVERY report when X2_REACHED_SELFTEST=1, and the run exits 4 if either fails: an inserted address reports REACHED, a never-inserted one reports NEVER, and the first-entry ordering of two insertions is checked to be increasing. Denominator printed alongside ('1358 distinct entry points were entered'), so NEVER cannot be confused with 'the instrument never ran'. Ambiguity is stated: an address is probed only against modules whose LINKED range contains it, after a first version wrongly reported an exe address as also being in libIGUtils.

## Known failure modes

(none recorded yet)
