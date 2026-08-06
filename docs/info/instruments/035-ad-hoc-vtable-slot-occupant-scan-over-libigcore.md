---
id: I035
kind: instrument
status: trusted
created: 2026-08-06
---

## Instrument

Ad-hoc vtable-slot occupant scan over libIGCore (inline in a session shell, NOT a committed tool) -- DISTRUSTED

## Validated by

CAUGHT LYING, do not reuse. It collected 'vtable starts' as any code immediate pointing at 21+ consecutive function entries, then reported the most common occupant per slot index. The output is self-evidently impossible: Gap::Core::igMetaField::commission appears as the majority occupant of slots 1, 3, 4, 7, 11, 17 AND 18 in 216-217 of 222 sampled vtables, and igClassNameMemoryTrackingScope::~igClassNameMemoryTrackingScope in most of the rest. One function cannot hold seven different slots across nearly every class. The defect is that a pointer INTO the middle of a function-pointer array passes the same test as its START, so most candidates are arbitrary offsets and the slot index is meaningless. Anything derived from it must be re-established. Note the one conclusion drawn before it was caught -- 'slot 20 is getClassMeta' -- happens to be independently corroborated by igObject::constructDerived, which dispatches [vptr+0x50] and then increments +0x2c and calls [+0x30] on the result, both meta-shaped; and implementing it advanced construction past that slot. That corroboration, not this scan, is why the slot 20 identification stands. A trustworthy replacement needs the same treatment tools/ark_vtables.py got: boundaries bracketed from both sides and disagreements printed.

## Known failure modes

(none recorded yet)
