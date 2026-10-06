---
id: I029
kind: instrument
status: trusted
created: 2026-08-06
---

## Instrument

X2_PEEK string and dword-run modes (:s, :*s, :dN)

## Validated by

Added because the 4-byte-only version cost one whole game run per guess -- identifying a meta field's name meant re-running once per candidate offset. Validated by producing the answer in one run where the previous form had taken five: libIGCore+0x15b330:*s24 read '_data' and +0x15b334 read '_count', i.e. it follows a pointer and renders the string behind it. Both negatives are distinguishable and stated: a NULL pointer reports '(NULL, so no string to read)' rather than an empty string, and an unmapped address reports 'UNREADABLE (not mapped)' rather than empty quotes -- an empty pair of quotes reads as 'the string is empty' when it usually means the address was wrong. An unrecognised size spec is refused by name instead of defaulting to 4. The spec buffer was raised 512 -> 2048 bytes because a whole-object sweep is ~64 items and 512 silently TRUNCATED the tail, so part of the sweep was simply not read and nothing said so.

## Known failure modes

(none recorded yet)
