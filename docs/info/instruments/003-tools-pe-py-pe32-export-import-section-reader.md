---
id: I003
kind: instrument
status: trusted
created: 2026-08-04
---

## Instrument

tools/pe.py -- PE32 export/import/section reader

## Validated by

Export count agrees with an independent tool (winedump) at 898 for libIGDisplay.dll. Its ordinal-import detector was run against BOTH classes: reports 0 ordinal imports for libIGDisplay (the negative) and 25 for WS2_32.dll in the same scan (the positive), so it can distinguish them. Refuses on a missing file instead of returning empty.

## Known failure modes

(none recorded yet)
