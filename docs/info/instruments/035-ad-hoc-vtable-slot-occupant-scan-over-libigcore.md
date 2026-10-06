---
id: I035
kind: instrument
status: trusted
created: 2026-08-06
---

## Correction (2026-08-06, same session)

**The reasoning that condemned this was WRONG, and the note is fixed rather
than left standing.**

The scan was distrusted because its output looked impossible:
`igMetaField::commission` as the majority occupant of slots 1, 3, 4, 7, 11, 17
and 18, and one destructor across most of the rest. "One function cannot hold
seven different slots across nearly every class" -- except it can, and here it
does.

The cause is **identical COMDAT folding**. MSVC's linker merges functions with
identical bodies, so every do-nothing virtual in the engine collapses to a
single address and Ghidra names that address after one arbitrary contributing
symbol. Measured on `igErrorHandler`'s 21-slot vtable: only **5 distinct
addresses** fill it.

    0x10020770   `RET`                    fills 10 slots
    0x1004f580   `RET 0x4`                fills  7 slots
    0x100459d0   `MOV AL,0x1 ; RET 0x4`   fills  2 slots
    0x10046d00   igObject::createCopy
    0x1000eba0   igErrorHandler::getClassMeta

So the repeated names were the truth about the binary, not a defect in the
scan. Anything discarded on the strength of that argument should be revisited.

## Status

The scan's *other* stated weakness is real and unfixed: it accepts a pointer
INTO a function-pointer array as a vtable START, so slot indices from it are
only meaningful for candidates that genuinely are starts. Use
`tools/ark_vtables.py` instead, which takes vtable addresses from each class's
`retrieveVTablePointer` and brackets the length from both sides.

What this episode actually teaches is narrower and worth keeping: **a repeated
function address across vtable slots is evidence of ICF, not of a broken
reader**, and any tool reporting "slot occupants" by NAME will look incoherent
on an ICF-folded binary while being perfectly correct.
