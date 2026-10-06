---
id: I037
kind: instrument
status: trusted
created: 2026-08-06
distrusted_on: 2026-08-06
---

## Instrument

tools/whose_function.py -- which function contains an address, and whether splitting there would destroy an SEH-protected function

## Validated by

Run against BOTH classes, not reasoned about. POSITIVE: 0x005fac25, an address interior to FUN_005fac10, is flagged '<<< SEH PROLOGUE -- DO NOT SPLIT', prints the function's first three instructions (PUSH -0x1 | PUSH 0x679141 | MOV EAX,FS:[0x0]) and exits 1. NEGATIVES: 0x00500000 (in no detected function) and 0x005facd5 (is itself a function entry) are both reported as such and exit 0 -- and the two negatives are distinguished from each other, because 'creates a function' and 'nothing to carve' need different responses. It refuses with a non-zero exit when given no addresses rather than printing a clean bill of health for an empty list. Use it before any analysis-database split; carving without this check can corrupt the recovered function boundary.

## Known failure modes

(none recorded yet)

## DISTRUSTED 2026-08-06

OVERCLAIMS, found by using it. whose_function.py prints '<<< SEH PROLOGUE -- DO NOT SPLIT' for any address inside a function whose entry installs an exception frame, and that criterion is too coarse: it is a reason for SUSPICION, not a verdict.

Demonstrated in the same region it was written for. After merging FUN_005fac10 back together (56 -> 426 instructions), the run dispatched indirectly to 0x005facd5 -- an address the merge had absorbed, now INSIDE the SEH function, which the tool therefore flags DO NOT SPLIT. But 0x005facd5 begins PUSH EBP; PUSH 0x6a3d08; PUSH 0x4, which is a plausible function entry, and the merge tool itself REFUSED to absorb its neighbour 0x005face5 on the grounds that 'the inner function is real'. So this region holds a MIX: 0x005fad31 and 0x005fb2bc begin mid-flow and were bad boundaries, while 0x005facd5 and 0x005face5 look like genuine indirectly-called entry points. The merge over-corrected by absorbing 0x005facd5, and the tool's flag would talk the next session out of restoring it.

The tool's other two answers are unaffected and still validated: 'in NO detected function' and 'IS the entry of' are both factual and were checked against real cases.

FIX: keep printing the containing function's first instructions -- that part is what made the mix visible -- but downgrade the SEH line from a verdict to evidence, and print the CANDIDATE's own first instructions alongside, since 'the address starts with a plausible prologue' is the thing that distinguishes 0x005facd5 from 0x005fad31 and the tool currently never shows it.

> Every result this instrument produced is suspect until it is re-validated.


## Re-validated 2026-08-06, after the overclaim above was fixed

The tool now prints the CANDIDATE's own first instructions beside the
container's, and the SEH line reads "container has an SEH prologue" --
evidence -- rather than "DO NOT SPLIT".

Run against all four classes:

  * `0x005fac25`, mid-container: container starts `PUSH -0x1 | PUSH 0x679141 |
    MOV EAX,FS:[0x0]`, candidate starts `SUB ESP,0x60 | PUSH EBX | PUSH EBP`.
    Flagged, exit 1.
  * `0x005facd5`, orphaned code: "in NO detected function -- a seed here
    creates one, it does not carve".
  * a function entry: "IS the entry of ... -- nothing to carve".
  * no addresses, and an empty address file: both REFUSE with exit 1 rather
    than reporting a clean bill of health for nothing.

The two `starts:` lines side by side are the point: they distinguish the two
kinds of address the earlier version conflated, which is what the overclaim
above was.
