---
id: I014
kind: instrument
status: trusted
created: 2026-08-05
---

## Instrument

-Wl,--wrap native override of a recompiled function (xbox/CMakeLists.txt + __wrap_sub_* in recomp_manual.c)

## Validated by

PARTIAL, and it fails SILENTLY. Verified on 2026-08-05 that a --wrap wrapper does NOT fire when the calling function lives in the same generated chunk as the callee: __wrap_sub_00275920 reported '0 calls' on a run whose crash stack contained sub_00275920+0x318 (issue #4). --wrap only rewrites references that cross an object-file boundary, and which chunk a caller lands in is an accident of --split 1000. POSITIVE CASE: moving the same observer to sub_003D5890 (memcpy, defined in recomp_0021.c, callers elsewhere) fired on call #413 with the expected implausible length -- so the mechanism does work across chunks. BEFORE TRUSTING ANY --wrap OVERRIDE: confirm it fires with a counter in the run report, and check grep -c 'RECOMP_DCALL(sub_X,' against the chunk that defines sub_X. A wrapper that reports nothing is indistinguishable from a code path that is never taken.

## Known failure modes

(none recorded yet)
