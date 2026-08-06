---
id: C105
kind: claim
status: holds
created: 2026-08-06
tags: pc,recomp,native,host,crt
---

## Claim

The scanf family is implemented on the guest stack, and its refusals located two real bugs of its own

## Evidence

sscanf and fscanf now walk the guest argument list the same way the printf side does, converting one directive at a time with the host's sscanf and using %n to advance by what actually matched rather than by a guess. Each conversion stores exactly its own width -- a four-byte store for a %hd would corrupt whatever follows it in the guest's struct -- and an unimplemented conversion REFUSES by name rather than reporting a match it did not make, because a scanf that silently matches nothing returns a count the caller believes and then reads uninitialised locals. That refusal earned its keep twice in one sitting: it stopped on %[ (scansets, unimplemented) and then, once those were added, on 'unterminated scanset' -- which was NOT a malformed format in the game but a 64-byte spec buffer too small for the engine's 67-character identifier scanset. The right refusal for the wrong reason, and it would have been read as a game bug if the message had not named the format. Both fixed; the buffer is 320 and the long scanset is a test case. tests/test_vformat.c now has 21 known-answer checks across both walkers. MEASURED: pairs entered 4161 -> 4172, battery 33/33, ctest 5/5, and the run advances past the CRT into ADVAPI32 (the Windows registry).

## What would falsify it

fscanf is line-oriented: it reads a line and scans it, where C's fscanf can stop mid-line and leave the rest for the next call. The engine's uses look like line-based config parsing, but that is an observation about the calls seen so far, not a proof -- a caller that scans a file field by field across line boundaries would silently lose data rather than refuse.
