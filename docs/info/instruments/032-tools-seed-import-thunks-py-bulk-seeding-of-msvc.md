---
id: I032
kind: instrument
status: trusted
created: 2026-08-06
---

## Instrument

tools/seed_import_thunks.py: bulk seeding of MSVC import jump thunks

## Validated by

CAUGHT ITSELF LYING FIRST, which is the validation worth recording. The first version parsed the PE import directory itself and reported 450,370 IAT slots for libIGGfx -- then found ZERO thunks. A confident zero from a parser reading past the descriptor array, and it would have been believed if the slot count had not been absurd on its face. Rewritten to consume , the project's verified PE reader (I003), whose output is already generated for every module: the counts became 989 slots for XMen2.exe (matching its import table) and 208 thunks, 543 and 240 for libIGGfx, 139 and 29 for libIGCore. Precision comes from the IAT rather than the byte pattern:  followed by four arbitrary bytes occurs in ordinary code (50 such in the exe) but  followed by a KNOWN IAT slot address is a thunk and essentially nothing else, and both classes are counted and printed. It refuses outright if the .iat lists no slots rather than reporting zero candidates from nothing.

## Known failure modes

(none recorded yet)
