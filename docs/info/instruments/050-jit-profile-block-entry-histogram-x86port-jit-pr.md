---
id: I050
kind: instrument
status: trusted
created: 2026-09-03
---

## Instrument

jit.profile block-entry histogram (x86port jit_profile + x2_engine_report top-40)

## Validated by

test_jit_profile: 3 unit tests (rank-by-entries, full-table drops-new-keeps-held, empty/degenerate) + test_jit_engine integration test 'test_profile_weights_a_block_by_how_often_it_is_entered' which drives a 2-block program (spin block entered >190x, once-entered block 1x) and asserts the profile top[0] is the spin block and total_hits==blocks_entered. In-game it produced a non-uniform distribution (hottest 3.6%, named symbols resolved e.g. igAttrStack::customReset) -- not the all-equal or all-zero output a broken histogram gives.

## Known failure modes

(none recorded yet)
