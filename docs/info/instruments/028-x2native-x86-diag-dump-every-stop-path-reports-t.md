---
id: I028
kind: instrument
status: trusted
created: 2026-08-06
---

## Instrument

x2native x86_diag_dump: every stop path reports the same set

## Validated by

Found by observing the instrument go silent: a run that stopped on a missing body (x86_dispatch -> abort) printed a ring and nothing else, because the reached-set and argument-watch reports were registered with atexit and abort() does not run atexit handlers. So the instruments were quiet on exactly the failures worth reporting. Consolidated into one x86_diag_dump() -- peek, reached, args, ring -- called from every abort path and from the SIGSEGV handler. VALIDATED: the same run that previously printed zero [REACHED] lines now prints the selftest (all five directions correct), the denominator (1398 pairs) and the per-address verdicts.

## Known failure modes

(none recorded yet)
